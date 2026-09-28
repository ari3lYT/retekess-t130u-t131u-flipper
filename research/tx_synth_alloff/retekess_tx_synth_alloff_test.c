#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#include "frame_synth_alloff.h"

#define TX_HZ 863975000u
#define REPEATS 64u
#define BIT_US 4u
#define LOG_PATH "/ext/retekess_probe/tx_synth_alloff_test.csv"

typedef struct {
    volatile uint32_t bit_index;
    volatile uint32_t callback_count;
    uint32_t total_bits;
} TxStream;

typedef struct {
    char phase[32];
    uint32_t actual_hz;
    uint32_t sent_bits;
} Screen;

static LevelDuration next_bit(void* context) {
    TxStream* s = context;
    uint32_t i = s->bit_index;
    if(i >= s->total_bits) return level_duration_reset();
    s->bit_index = i + 1;
    s->callback_count++;
    uint32_t frame_index = i % FRAME_BITS;
    bool level = (frame_bytes[frame_index / 8] >> (7 - frame_index % 8)) & 1;
    return level_duration_make(level, BIT_US);
}

static void draw(Canvas* canvas, void* context) {
    Screen* s = context;
    char line[36];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 13, "Retekess synth AllOff");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 31, s->phase);
    snprintf(line, sizeof(line), "%lu Hz, -10 dBm", s->actual_hz);
    canvas_draw_str(canvas, 2, 46, line);
    snprintf(line, sizeof(line), "bits %lu/%u", s->sent_bits, REPEATS * FRAME_BITS);
    canvas_draw_str(canvas, 2, 60, line);
}

int32_t retekess_tx_synth_alloff_test_app(void* p) {
    UNUSED(p);
    Screen screen = {.phase = "Prepare: T130U OFF", .actual_hz = TX_HZ};
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    ViewPort* view = view_port_alloc();
    view_port_draw_callback_set(view, draw, &screen);
    gui_add_view_port(gui, view, GuiLayerFullscreen);

    furi_delay_ms(3000);
    furi_hal_subghz_idle();
    furi_hal_subghz_reset();
    static const uint8_t preset[] = {
        // Receiver-derived 250 kbaud GFSK hypothesis; TX mode remains experimental.
        0x02, 0x0D, 0x03, 0x47, 0x07, 0x04, 0x08, 0x32,
        0x0B, 0x0C, 0x10, 0x2D, 0x11, 0x3B, 0x12, 0x10,
        0x13, 0x22, 0x14, 0xF8, 0x15, 0x62, 0x18, 0x18,
        0x19, 0x1D, 0x1A, 0x1C, 0x1B, 0xC7, 0x1C, 0x00,
        0x1D, 0xB0, 0x20, 0xFB, 0x21, 0xB6, 0x22, 0x10,
        0x23, 0x59, 0x2C, 0x81, 0x2D, 0x35, 0x2E, 0x09,
        0x00, 0x00,
        0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    furi_hal_subghz_load_custom_preset(preset);
    screen.actual_hz = furi_hal_subghz_set_frequency_and_path(TX_HZ);
    bool allowed = furi_hal_subghz_is_tx_allowed(screen.actual_hz);
    bool started = false;
    bool completed = false;
    TxStream stream = {.total_bits = FRAME_BITS * REPEATS};
    uint32_t start_tick = furi_get_tick();
    if(allowed) {
        strcpy(screen.phase, "Sending 0.508 sec...");
        view_port_update(view);
        started = furi_hal_subghz_start_async_tx(next_bit, &stream);
        if(started) {
            start_tick = furi_get_tick();
            while(furi_get_tick() - start_tick < 1800) {
                if(furi_hal_subghz_is_async_tx_complete()) {
                    completed = true;
                    break;
                }
                furi_delay_ms(5);
            }
            furi_hal_subghz_stop_async_tx();
        }
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    screen.sent_bits = stream.bit_index;
    strcpy(screen.phase, completed ? "TX finished" : (allowed ? "TX timeout/error" : "TX blocked"));
    view_port_update(view);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, "/ext/retekess_probe");
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[192];
        int n = snprintf(line, sizeof(line),
            "requested_hz,actual_hz,allowed,started,completed,bit_us,repeats,bits_delivered,callbacks,elapsed_tick_ms\n"
            "%u,%lu,%u,%u,%u,%u,%u,%lu,%lu,%lu\n",
            TX_HZ, screen.actual_hz, allowed, started, completed, BIT_US, REPEATS,
            stream.bit_index, stream.callback_count, furi_get_tick() - start_tick);
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
