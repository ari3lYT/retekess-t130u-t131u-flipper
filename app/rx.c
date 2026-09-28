#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <storage/storage.h>
#include <lib/drivers/cc1101_regs.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>
#include "frame_generated.h"

#define SAMPLE_COUNT 16384
#define BIT_PERIOD 1984
#define FIELD_START 529
#define FIELD_LENGTH 16
#define MODE_CHS 20
#define LOG_DIR "/ext/retekess_probe"
#define LOG_PATH LOG_DIR "/suite_monitor.csv"
#define EVENT_PATH LOG_DIR "/suite_events.csv"
#define TX_REPEATS 64u
#define TX_BIT_US 4u
#define TX_NONE 0u
#define TX_ALLOFF 1u
#define TX_CHS18 2u
#define TX_CHS19 3u
#define TX_CHS1 4u

static uint8_t samples[SAMPLE_COUNT];

typedef struct {
    volatile bool running;
    volatile uint8_t tx_requested;
    volatile uint8_t requested_channel;
    uint8_t channel;
    uint32_t actual_hz;
    uint32_t blocks;
    uint32_t marker_hits;
    uint32_t alloff_hits;
    uint32_t tx_sent;
    uint32_t last_tx_unix_s;
    uint8_t last_tx_cmd;
    bool last_tx_ok;
    uint32_t last_marker_unix_s;
    uint16_t last_field;
    bool field_valid;
} MonitorState;

typedef struct {
    volatile uint32_t bit_index;
    uint32_t total_bits;
    const uint8_t* frame;
} TxStream;

// Receiver hypothesis measured against CH18/CH19/CHS raw captures and reused
// for the empirically tested AllOff replay. It does not assert that T130U uses
// the same CC1101 register values.
static const uint8_t rx_preset[] = {
    CC1101_IOCFG0, 0x0D,
    CC1101_FIFOTHR, 0x47,
    CC1101_PKTCTRL1, 0x04,
    CC1101_PKTCTRL0, 0x32,
    CC1101_FSCTRL1, 0x0C,
    CC1101_MDMCFG4, 0x2D,
    CC1101_MDMCFG3, 0x3B,
    CC1101_MDMCFG2, 0x10,
    CC1101_MDMCFG1, 0x22,
    CC1101_MDMCFG0, 0xF8,
    CC1101_DEVIATN, 0x62,
    CC1101_MCSM0, 0x18,
    CC1101_FOCCFG, 0x1D,
    CC1101_BSCFG, 0x1C,
    CC1101_AGCCTRL2, 0xC7,
    CC1101_AGCCTRL1, 0x00,
    CC1101_AGCCTRL0, 0xB0,
    CC1101_WORCTRL, 0xFB,
    CC1101_FREND1, 0xB6,
    CC1101_FREND0, 0x10,
    CC1101_FSTEST, 0x59,
    CC1101_TEST2, 0x81,
    CC1101_TEST1, 0x35,
    CC1101_TEST0, 0x09,
    0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static LevelDuration replay_next_bit(void* context) {
    TxStream* stream = context;
    uint32_t bit = stream->bit_index;
    if(bit >= stream->total_bits) return level_duration_reset();
    stream->bit_index = bit + 1;
    uint32_t frame_bit = bit % FRAME_BITS;
    bool level = (stream->frame[frame_bit / 8] >> (7 - frame_bit % 8)) & 1;
    return level_duration_make(level, TX_BIT_US);
}

static uint8_t frame_bit(const uint8_t* frame, uint32_t bit) {
    return (frame[bit / 8] >> (7 - bit % 8)) & 1u;
}

static void set_frame_bit(uint8_t* frame, uint32_t bit, uint8_t value) {
    uint8_t mask = 1u << (7 - bit % 8);
    if(value) frame[bit / 8] |= mask;
    else frame[bit / 8] &= ~mask;
}

// Candidate CHS field for channels 1..19. CH1/18/19 were physically verified
// with captured audio templates; other targets and these silent templates await testing.
static void make_target_frame(uint8_t* out, uint8_t channel) {
    memcpy(out, chs1_frame, 248);
    uint16_t field = 0x1100u | channel;
    for(uint8_t i = 0; i < 16; i++) set_frame_bit(out, 529 + i, (field >> (15 - i)) & 1u);
    uint16_t crc = 0x1D0F;
    for(uint32_t i = 33; i < 545; i++) {
        bool carry = ((crc >> 15) ^ frame_bit(out, i)) & 1;
        crc = (uint16_t)((crc << 1) ^ (carry ? 0x1021 : 0));
    }
    for(uint8_t i = 0; i < 16; i++) set_frame_bit(out, 545 + i, (crc >> (15 - i)) & 1u);
}

static uint32_t marker_for_channel(uint8_t ch) {
    if(ch == 1) return 0x9B1B9C1C;
    if(ch == 17) return 0x9B1BA424;
    if(ch == 18) return 0x9B1BA4A5;
    if(ch == MODE_CHS) return 0x9B1BA5A6;
    return 0x9B1BA525;
}

static uint32_t frequency_for_channel(uint8_t ch) {
    if(ch == 1) return 823475000;
    if(ch == 17) return 831475000;
    if(ch == 18) return 863475000;
    if(ch == MODE_CHS) return 864475000;
    return 863975000;
}

static uint8_t next_mode(uint8_t ch) {
    if(ch == 1) return 17;
    if(ch == 17) return 18;
    if(ch == 18) return 19;
    if(ch == 19) return MODE_CHS;
    return 1;
}

static uint8_t previous_mode(uint8_t ch) {
    if(ch == 1) return MODE_CHS;
    if(ch == 17) return 1;
    if(ch == 18) return 17;
    if(ch == 19) return 18;
    return 19;
}

static void draw_screen(Canvas* canvas, void* context) {
    const MonitorState* s = context;
    char line[40];
    uint32_t now = furi_hal_rtc_get_timestamp();
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(s->channel == MODE_CHS) snprintf(line, sizeof(line), "Retekess RX  CHS");
    else snprintf(line, sizeof(line), "Retekess RX  CH%u", s->channel);
    canvas_draw_str(canvas, 2, 11, line);
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "%lu.%03lu MHz", s->actual_hz / 1000000, (s->actual_hz / 1000) % 1000);
    canvas_draw_str(canvas, 2, 23, line);
    snprintf(line, sizeof(line), "CRC frame: %s (%lu)",
             s->last_marker_unix_s && now - s->last_marker_unix_s < 2 ? "YES" : "--",
             s->marker_hits);
    canvas_draw_str(canvas, 2, 35, line);
    if(s->channel == MODE_CHS && s->field_valid) {
        if(s->last_field == 0x1101) snprintf(line, sizeof(line), "Target CH1  1101");
        else if(s->last_field == 0x1110) snprintf(line, sizeof(line), "Target CH16 1110");
        else if(s->last_field == 0x1112) snprintf(line, sizeof(line), "Target CH18 1112");
        else if(s->last_field == 0x1113) snprintf(line, sizeof(line), "Target CH19 1113");
        else snprintf(line, sizeof(line), "CHS field: %04X", s->last_field);
    } else if(s->field_valid && s->last_field == 0x7777)
        snprintf(line, sizeof(line), "All Off  7777");
    else if(s->field_valid) snprintf(line, sizeof(line), "Field 529: %04X", s->last_field);
    else snprintf(line, sizeof(line), "Field 529: ----");
    canvas_draw_str(canvas, 2, 47, line);
    snprintf(line, sizeof(line), "< > channel   Back menu");
    canvas_draw_str(canvas, 2, 59, line);
}

static void input_callback(InputEvent* event, void* context) {
    MonitorState* s = context;
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyBack) s->running = false;
    else if(event->key == InputKeyLeft) {
        s->requested_channel = previous_mode(s->requested_channel);
    } else if(event->key == InputKeyRight) {
        s->requested_channel = next_mode(s->requested_channel);
    }
}

static uint32_t sample_gpio(uint32_t sample_cycles) {
    GPIO_TypeDef* port = gpio_cc1101_g0.port;
    uint16_t pin = gpio_cc1101_g0.pin;
    uint32_t late = 0;
    uint32_t previous_irq = __get_PRIMASK();
    __disable_irq();
    uint32_t next = DWT->CYCCNT + sample_cycles;
    for(size_t i = 0; i < SAMPLE_COUNT; ++i) {
        uint32_t now;
        do { now = DWT->CYCCNT; } while((int32_t)(now - next) < 0);
        if(now - next > sample_cycles / 2) late++;
        samples[i] = (port->IDR & pin) != 0;
        next += sample_cycles;
    }
    __set_PRIMASK(previous_irq);
    return late;
}

static uint8_t sampled_bit(uint8_t phase, size_t bit_index) {
    return samples[phase + bit_index * 8];
}

// Search all eight half-microsecond phases. Report a field only when the
// complete frame prefix is in this sample block and its CRC-16/AUG-CCITT
// over bits 33..544 matches the captured bits 545..560.
static bool decode_field(uint32_t marker, uint16_t* field) {
    for(uint8_t phase = 0; phase < 8; phase++) {
        size_t bit_count = (SAMPLE_COUNT - 1 - phase) / 8 + 1;
        uint32_t window = 0;
        for(size_t i = 0; i < bit_count; i++) {
            window = (window << 1) | sampled_bit(phase, i);
            if(i >= 31 && window == marker) {
                size_t start = i - 31;
                if(start + 561 > bit_count) continue;
                uint16_t crc = 0x1D0F;
                for(size_t j = 33; j < 545; j++) {
                    uint8_t bit = sampled_bit(phase, start + j);
                    bool carry = ((crc >> 15) ^ bit) & 1;
                    crc = (uint16_t)((crc << 1) ^ (carry ? 0x1021 : 0));
                }
                uint16_t captured_crc = 0;
                for(size_t j = 545; j < 561; j++) {
                    captured_crc = (uint16_t)((captured_crc << 1) |
                                              sampled_bit(phase, start + j));
                }
                if(crc != captured_crc) continue;
                uint16_t value = 0;
                for(size_t j = 0; j < FIELD_LENGTH; j++) {
                    value = (value << 1) | sampled_bit(phase, start + FIELD_START + j);
                }
                *field = value;
                return true;
            }
        }
    }
    return false;
}

static void tune_receiver(MonitorState* s) {
    furi_hal_subghz_idle();
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(rx_preset);
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
    s->actual_hz = furi_hal_subghz_set_frequency_and_path(frequency_for_channel(s->channel));
    furi_hal_subghz_rx();
    furi_delay_ms(70);
    s->blocks = s->marker_hits = s->alloff_hits = 0;
    s->last_marker_unix_s = 0;
    s->last_field = 0;
    s->field_valid = false;
}

// Four independently tested generated command frames. All Off is sent on
// CH19; CHS target fields are sent on the measured 864.475 MHz service band.
static bool transmit_command(MonitorState* s, uint8_t command) {
    uint8_t candidate[248];
    if(command >= 5 && command <= 23) make_target_frame(candidate, command - 4);
    const uint8_t* frame = command >= 5 && command <= 23 ? candidate :
                           command == TX_ALLOFF ? alloff_frame :
                           command == TX_CHS1 ? chs1_frame :
                           command == TX_CHS18 ? chs18_frame : chs19_frame;
    uint32_t hz = command == TX_ALLOFF ? 863975000u : 864475000u;
    uint32_t repeats = command == TX_ALLOFF ? 64u : 512u;
    furi_hal_subghz_idle();
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(rx_preset);
    const uint8_t pa[8] = {0x27, 0, 0, 0, 0, 0, 0, 0};
    furi_hal_subghz_load_patable(pa);
    uint32_t actual_hz = furi_hal_subghz_set_frequency_and_path(hz);
    if(!furi_hal_subghz_is_tx_allowed(actual_hz)) return false;
    TxStream stream = {.total_bits = FRAME_BITS * repeats, .frame = frame};
    if(!furi_hal_subghz_start_async_tx(replay_next_bit, &stream)) return false;
    uint32_t start = furi_get_tick();
    bool completed = false;
    while(furi_get_tick() - start < 6000) {
        if(furi_hal_subghz_is_async_tx_complete()) {
            completed = true;
            break;
        }
        furi_delay_ms(5);
    }
    furi_hal_subghz_stop_async_tx();
    if(completed && stream.bit_index == stream.total_bits) s->tx_sent++;
    return completed && stream.bit_index == stream.total_bits;
}

bool suite_tx_command(uint8_t command) {
    MonitorState s = {0};
    bool ok = transmit_command(&s, command);
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    return ok;
}

int32_t suite_rx_monitor(void* p) {
    UNUSED(p);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t sample_cycles = SystemCoreClock / 2000000;
    if(sample_cycles < 20 || sample_cycles > 50) return -1;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, LOG_DIR);
    File* file = storage_file_alloc(storage);
    File* events = storage_file_alloc(storage);
    bool log_ok = storage_file_open(file, LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    bool event_ok = storage_file_open(events, EVENT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(event_ok) {
        const char* header = "unix_s,channel,field,event,target_ch\n";
        event_ok = storage_file_write(events, header, strlen(header)) == strlen(header);
    }
    if(log_ok) {
        const char* header = "unix_s,receiver_mode,actual_hz,blocks,marker_hits,alloff_hits,tx_sent,last_tx_cmd,last_field,field_valid,late_samples\n";
        log_ok = storage_file_write(file, header, strlen(header)) == strlen(header);
    }

    uint8_t initial_channel = (uint8_t)(uintptr_t)p;
    if(initial_channel != 1 && initial_channel != 17 && initial_channel != 18 &&
       initial_channel != 19 && initial_channel != MODE_CHS) initial_channel = 19;
    MonitorState s = {.running = true, .channel = initial_channel, .requested_channel = initial_channel};
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_screen, &s);
    view_port_input_callback_set(view_port, input_callback, &s);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    tune_receiver(&s);
    uint32_t last_log_unix_s = furi_hal_rtc_get_timestamp();
    uint32_t last_draw_tick = furi_get_tick();
    uint32_t late_sum = 0;
    uint16_t last_logged_field = 0xFFFF;
    uint8_t last_logged_mode = 0;
    while(s.running) {
        if(s.tx_requested) {
            uint8_t command = s.tx_requested;
            s.tx_requested = TX_NONE;
            uint32_t now_s = furi_hal_rtc_get_timestamp();
            if(!s.last_tx_unix_s || now_s - s.last_tx_unix_s >= 2) {
                s.last_tx_cmd = command;
                s.last_tx_ok = transmit_command(&s, command);
                s.last_tx_unix_s = furi_hal_rtc_get_timestamp();
                tune_receiver(&s);
                view_port_update(view_port);
            }
        }
        if(s.requested_channel != s.channel) {
            s.channel = s.requested_channel;
            tune_receiver(&s);
            last_logged_mode = 0;
            view_port_update(view_port);
        }
        late_sum += sample_gpio(sample_cycles);
        s.blocks++;
        uint16_t field;
        if(decode_field(marker_for_channel(s.channel), &field)) {
            s.marker_hits++;
            s.last_marker_unix_s = furi_hal_rtc_get_timestamp();
            s.last_field = field;
            s.field_valid = true;
            if(s.channel == 19 && field == 0x7777) {
                s.alloff_hits++;
            }
            if(event_ok && (field != last_logged_field || s.channel != last_logged_mode)) {
                const char* label = s.channel == MODE_CHS ? "CHS" :
                    field == 0x7777 ? "ALL_OFF" : field == 0 ? "AUDIO" : "OTHER";
                uint8_t target = s.channel == MODE_CHS && (field >> 8) == 0x11 ?
                    (field & 0xFF) : 0;
                char line[64];
                int n = snprintf(line, sizeof(line), "%lu,%u,%04X,%s,%u\n",
                    furi_hal_rtc_get_timestamp(), s.channel, field, label, target);
                event_ok = storage_file_write(events, line, n) == (size_t)n;
                last_logged_field = field;
                last_logged_mode = s.channel;
            }
        }
        uint32_t now = furi_get_tick();
        if(now - last_draw_tick >= 250) {
            view_port_update(view_port);
            last_draw_tick = now;
        }
        uint32_t unix_s = furi_hal_rtc_get_timestamp();
        if(unix_s != last_log_unix_s) {
            if(log_ok) {
                char line[120];
                size_t len = snprintf(line, sizeof(line), "%lu,%u,%lu,%lu,%lu,%lu,%lu,%u,%04X,%u,%lu\n",
                    unix_s, s.channel, s.actual_hz, s.blocks, s.marker_hits,
                    s.alloff_hits, s.tx_sent, s.last_tx_cmd, s.last_field,
                    s.field_valid ? 1 : 0, late_sum);
                log_ok = storage_file_write(file, line, len) == len;
            }
            last_log_unix_s = unix_s;
            late_sum = 0;
        }
        furi_delay_ms(2);
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    if(log_ok) storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    storage_file_close(events);
    storage_file_free(events);
    furi_record_close(RECORD_STORAGE);
    return 0;
}
