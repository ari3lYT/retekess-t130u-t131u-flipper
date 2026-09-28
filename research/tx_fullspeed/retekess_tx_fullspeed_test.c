#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define TX_HZ 863975000u
#define BIT_US 4u
#define REPEATS 5u
#define MAX_BYTES 64000u
#define DATA_PATH "/ext/retekess_probe/voice_frames_fullspeed.bin"
#define LOG_PATH "/ext/retekess_probe/tx_fullspeed_test.csv"

static uint8_t recording[MAX_BYTES];

typedef struct {
    volatile uint32_t bit_index;
    uint32_t payload_bits;
    uint32_t total_bits;
} TxStream;

typedef struct {
    char phase[30];
    uint32_t actual_hz;
    uint32_t source_bytes;
    uint32_t sent_bits;
} Screen;

static LevelDuration next_bit(void* context) {
    TxStream* s = context;
    uint32_t i = s->bit_index;
    if(i >= s->total_bits) return level_duration_reset();
    s->bit_index = i + 1;
    uint32_t pos = i % s->payload_bits;
    bool level = (recording[pos / 8] >> (7 - pos % 8)) & 1;
    return level_duration_make(level, BIT_US);
}

static void draw(Canvas* canvas, void* context) {
    Screen* s = context;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 13, "Retekess fullspeed TX");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 29, s->phase);
    snprintf(line, sizeof(line), "%lu bytes x %u", s->source_bytes, REPEATS);
    canvas_draw_str(canvas, 2, 44, line);
    snprintf(line, sizeof(line), "sent %lu bits", s->sent_bits);
    canvas_draw_str(canvas, 2, 58, line);
}

int32_t retekess_tx_fullspeed_test_app(void* p) {
    UNUSED(p);
    Screen screen = {.phase = "Read recording...", .actual_hz = TX_HZ};
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    ViewPort* view = view_port_alloc();
    view_port_draw_callback_set(view, draw, &screen);
    gui_add_view_port(gui, view, GuiLayerFullscreen);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool input_ok = storage_file_open(file, DATA_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    if(input_ok) {
        uint64_t size = storage_file_size(file);
        if(size > 1000 && size <= MAX_BYTES) {
            screen.source_bytes = size;
            input_ok = storage_file_read(file, recording, size) == size;
        } else input_ok = false;
        storage_file_close(file);
    }
    if(input_ok) {
        strcpy(screen.phase, "Prepare: T130U OFF");
        view_port_update(view);
        furi_delay_ms(3000);
    }

    bool allowed = false, started = false, completed = false;
    TxStream stream = {.payload_bits = screen.source_bytes * 8,
                       .total_bits = screen.source_bytes * 8 * REPEATS};
    uint32_t elapsed = 0;
    if(input_ok) {
        furi_hal_subghz_idle();
        furi_hal_subghz_reset();
        static const uint8_t preset[] = {
            0x02, 0x0D, 0x03, 0x47, 0x07, 0x04, 0x08, 0x32,
            0x0B, 0x0C, 0x10, 0x2D, 0x11, 0x3B, 0x12, 0x10,
            0x13, 0x22, 0x14, 0xF8, 0x15, 0x62, 0x18, 0x18,
            0x19, 0x1D, 0x1A, 0x1C, 0x1B, 0xC7, 0x1C, 0x00,
            0x1D, 0xB0, 0x20, 0xFB, 0x21, 0xB6, 0x22, 0x10,
            0x23, 0x59, 0x2C, 0x81, 0x2D, 0x35, 0x2E, 0x09,
            0x00, 0x00,
            0x27, 0, 0, 0, 0, 0, 0, 0,
        };
        furi_hal_subghz_load_custom_preset(preset);
        screen.actual_hz = furi_hal_subghz_set_frequency_and_path(TX_HZ);
        allowed = furi_hal_subghz_is_tx_allowed(screen.actual_hz);
        if(allowed) {
            strcpy(screen.phase, "TX ~8 seconds...");
            view_port_update(view);
            started = furi_hal_subghz_start_async_tx(next_bit, &stream);
            if(started) {
                uint32_t start = furi_get_tick();
                while(furi_get_tick() - start < 12000) {
                    if(furi_hal_subghz_is_async_tx_complete()) {
                        completed = true;
                        break;
                    }
                    furi_delay_ms(5);
                }
                elapsed = furi_get_tick() - start;
                furi_hal_subghz_stop_async_tx();
            }
        }
        furi_hal_subghz_idle();
        furi_hal_subghz_sleep();
    }
    screen.sent_bits = stream.bit_index;
    strcpy(screen.phase, completed ? "TX finished" : "TX error / no file");
    view_port_update(view);

    if(storage_file_open(file, LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[192];
        int n = snprintf(line, sizeof(line),
            "requested_hz,actual_hz,source_bytes,allowed,started,completed,bit_us,repeats,bits_delivered,elapsed_ms\n"
            "%u,%lu,%lu,%u,%u,%u,%u,%u,%lu,%lu\n",
            TX_HZ, screen.actual_hz, screen.source_bytes, allowed, started,
            completed, BIT_US, REPEATS, stream.bit_index, elapsed);
        if(n > 0 && (size_t)n < sizeof(line)) storage_file_write(file, line, n);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_delay_ms(4000);
    gui_remove_view_port(gui, view);
    view_port_free(view);
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    return completed ? 0 : -1;
}
