#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <storage/storage.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/drivers/cc1101_regs.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define TAG "RetekessDemod"
#define OUT_DIR "/ext/retekess_probe"
#define OUT_FILE OUT_DIR "/last_chs_old.csv"
#define SAMPLE_COUNT 16384
#define CAPTURE_DURATION_MS 28000

typedef struct {
    const char* name;
    uint32_t rate_bps;
    uint32_t deviation_hz;
    uint32_t bandwidth_hz;
    uint8_t mdmcfg2;
    uint8_t mdmcfg3;
    uint8_t mdmcfg4;
    uint8_t deviatn;
    uint8_t fsctrl1;
} Candidate;

// Baseband values from a SmartRF Studio export published on TI E2E for
// 250 kBaud GFSK. IOCFG0/PKTCTRL0 adapted for read-only async GDO0 sampling.
// These are receiver hypotheses, not presumed T130U settings.
static const Candidate candidates[] = {
    {"G250D127B542", 249939, 126953, 541667, 0x10, 0x3B, 0x2D, 0x62, 0x0C},
};

static uint8_t samples[SAMPLE_COUNT];
static char line[4600];

typedef struct {
    uint32_t remaining_seconds;
    uint8_t phase; // 0: ready, 1: capture, 2: complete
} ScreenState;

static void draw_screen(Canvas* canvas, void* context) {
    const ScreenState* state = context;
    char countdown[24];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 14, "OLD CHANNEL CH18");
    canvas_set_font(canvas, FontSecondary);
    if(state->phase == 0) {
        snprintf(countdown, sizeof(countdown), "READY: %lu s", state->remaining_seconds);
        canvas_draw_str(canvas, 3, 34, countdown);
        canvas_draw_str(canvas, 3, 53, "Watch this screen");
    } else if(state->phase == 1) {
        snprintf(countdown, sizeof(countdown), "RECORD: %lu s", state->remaining_seconds);
        canvas_draw_str(canvas, 3, 34, countdown);
        uint32_t elapsed_seconds = CAPTURE_DURATION_MS / 1000 - state->remaining_seconds;
        if(elapsed_seconds < 4) {
            canvas_draw_str(canvas, 3, 53, "WAIT: TX CH18");
        } else if(elapsed_seconds < 12) {
            canvas_draw_str(canvas, 3, 53, "SET TX CH19 NOW");
        } else {
            canvas_draw_str(canvas, 3, 53, "WAIT: RX CH19");
        }
    } else {
        canvas_draw_str(canvas, 3, 34, "RECORDING DONE");
        canvas_draw_str(canvas, 3, 53, "Thank you");
    }
}

static bool make_candidate_preset(uint8_t out[128], const Candidate* candidate) {
    const uint8_t source[] = {
        CC1101_IOCFG0, 0x0D,
        CC1101_FIFOTHR, 0x47,
        CC1101_PKTCTRL1, 0x04,
        CC1101_PKTCTRL0, 0x32,
        CC1101_FSCTRL1, candidate->fsctrl1,
        CC1101_MDMCFG4, candidate->mdmcfg4,
        CC1101_MDMCFG3, candidate->mdmcfg3,
        CC1101_MDMCFG2, candidate->mdmcfg2,
        CC1101_MDMCFG1, 0x22,
        CC1101_MDMCFG0, 0xF8,
        CC1101_DEVIATN, candidate->deviatn,
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
    if(sizeof(source) > 128) return false;
    memcpy(out, source, sizeof(source));
    return true;
}

static uint32_t measure_core_mhz(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return SystemCoreClock / 1000000;
}

static uint32_t sample_gpio(uint32_t sample_cycles) {
    GPIO_TypeDef* port = gpio_cc1101_g0.port;
    uint16_t pin = gpio_cc1101_g0.pin;
    uint32_t late = 0;
    uint32_t prior_interrupt_state = __get_PRIMASK();
    __disable_irq();
    uint32_t next = DWT->CYCCNT + sample_cycles;
    for(size_t i = 0; i < SAMPLE_COUNT; ++i) {
        uint32_t now;
        do {
            now = DWT->CYCCNT;
        } while((int32_t)(now - next) < 0);
        if(now - next > sample_cycles / 2) late++;
        samples[i] = (port->IDR & pin) != 0;
        next += sample_cycles;
    }
    __set_PRIMASK(prior_interrupt_state);
    return late;
}

static bool save_block(
    File* file,
    size_t candidate_id,
    size_t block,
    uint32_t elapsed_ms,
    const Candidate* candidate,
    uint32_t actual_hz,
    int32_t rssi_x10,
    uint32_t core_mhz,
    uint32_t sample_period_ns,
    uint32_t late) {
    size_t used = snprintf(
        line,
        sizeof(line),
        "%lu,%lu,%lu,%s,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,",
        (uint32_t)candidate_id,
        (uint32_t)block,
        elapsed_ms,
        candidate->name,
        candidate->rate_bps,
        candidate->deviation_hz,
        candidate->bandwidth_hz,
        actual_hz,
        rssi_x10,
        core_mhz,
        sample_period_ns,
        late);
    const char hex[] = "0123456789ABCDEF";
    uint32_t ones = 0;
    for(size_t i = 0; i < SAMPLE_COUNT; i += 8) {
        uint8_t byte = 0;
        for(size_t bit = 0; bit < 8; ++bit) {
            byte = (byte << 1) | samples[i + bit];
            ones += samples[i + bit];
        }
        line[used++] = hex[byte >> 4];
        line[used++] = hex[byte & 15];
    }
    // Append ones count after the packed samples to avoid extra work while sampling.
    used += snprintf(line + used, sizeof(line) - used, ",%lu\n", ones);
    return storage_file_write(file, line, used) == used;
}

int32_t retekess_chs_old_probe_app(void* p) {
    UNUSED(p);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, OUT_DIR);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, OUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        const char* header =
            "candidate,block,elapsed_ms,mode,rate_bps,deviation_hz,bandwidth_hz,actual_hz,"
            "rssi_dbm_x10,core_mhz,sample_period_ns,late_samples,packed_gpio,ones\n";
        ok = storage_file_write(file, header, strlen(header)) == strlen(header);
    }
    uint32_t core_mhz = measure_core_mhz();
    if(core_mhz < 50 || core_mhz > 100) ok = false;
    uint8_t preset[128] = {0};
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    ScreenState screen = {.remaining_seconds = 3, .phase = 0};
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_screen, &screen);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    for(uint32_t seconds = 3; seconds > 0; --seconds) {
        screen.remaining_seconds = seconds;
        view_port_update(view_port);
        furi_delay_ms(1000);
    }
    for(size_t c = 0; c < COUNT_OF(candidates) && ok; ++c) {
        const Candidate* candidate = &candidates[c];
        ok = make_candidate_preset(preset, candidate);
        if(!ok) break;
        furi_hal_subghz_reset();
        furi_hal_subghz_load_custom_preset(preset);
        furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
        for(uint32_t requested_hz = 863475000; requested_hz <= 863475000 && ok;
            requested_hz += 50000) {
            uint32_t actual_hz = furi_hal_subghz_set_frequency_and_path(requested_hz);
            furi_hal_subghz_rx();
            furi_delay_ms(70);
            uint32_t capture_start = furi_get_tick();
            screen.phase = 1;
            screen.remaining_seconds = CAPTURE_DURATION_MS / 1000;
            view_port_update(view_port);
            for(size_t block = 0; ok && (furi_get_tick() - capture_start) < CAPTURE_DURATION_MS;
                ++block) {
                uint32_t elapsed_ms = furi_get_tick() - capture_start;
                uint32_t remaining_seconds = (CAPTURE_DURATION_MS - elapsed_ms + 999) / 1000;
                if(remaining_seconds != screen.remaining_seconds) {
                    screen.remaining_seconds = remaining_seconds;
                    view_port_update(view_port);
                }
                uint32_t late = sample_gpio(core_mhz / 2);
                int32_t rssi_x10 = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
                ok = save_block(file, c, block, elapsed_ms, candidate, actual_hz, rssi_x10, core_mhz, 500, late);
                furi_delay_ms(2);
            }
            furi_hal_subghz_idle();
        }
    }
    screen.phase = 2;
    view_port_update(view_port);
    furi_delay_ms(1600);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_hal_subghz_sleep();
    if(ok) ok = storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I(TAG, "Demod probe %s", ok ? "complete" : "failed");
    return ok ? 0 : -1;
}
