#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define OUT_DIR "/ext/retekess_probe"
#define OUT_FILE OUT_DIR "/chs_fine_spectrum.csv"
#define CAPTURE_MS 28000

typedef struct { File* file; char buffer[1024]; size_t used; bool ok; } CsvWriter;
typedef struct { uint8_t phase; uint32_t remaining; uint32_t pass; } Screen;

static void flush(CsvWriter* w) {
    if(w->ok && w->used) w->ok = storage_file_write(w->file, w->buffer, w->used) == w->used;
    w->used = 0;
}
static void line(CsvWriter* w, const char* s) {
    if(!w->ok) return;
    size_t n = strlen(s);
    if(n + w->used > sizeof(w->buffer)) flush(w);
    if(!w->ok || n > sizeof(w->buffer)) { w->ok = false; return; }
    memcpy(w->buffer + w->used, s, n); w->used += n;
}
static void draw(Canvas* c, void* ctx) {
    const Screen* s = ctx; char msg[32];
    canvas_clear(c); canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 12, "CHS fine 863-865"); canvas_set_font(c, FontSecondary);
    if(s->phase == 0) {
        snprintf(msg, sizeof(msg), "READY %lu sec", s->remaining);
        canvas_draw_str(c, 2, 34, msg); canvas_draw_str(c, 2, 55, "Keep TX on CH19");
    } else if(s->phase == 1) {
        snprintf(msg, sizeof(msg), "RECORD %lu sec", s->remaining);
        canvas_draw_str(c, 2, 30, msg);
        snprintf(msg, sizeof(msg), "Pass %lu", s->pass); canvas_draw_str(c, 2, 42, msg);
        canvas_draw_str(c, 2, 58, s->remaining > 24 ? "WAIT TX CH19" :
                        s->remaining > 12 ? "SET TX CH18 NOW" : "WAIT, NO PRESS");
    } else canvas_draw_str(c, 2, 36, "CAPTURE DONE");
}
int32_t retekess_chs_fine_probe_app(void* p) {
    UNUSED(p);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, OUT_DIR);
    File* file = storage_file_alloc(storage);
    CsvWriter w = {.file = file, .ok = storage_file_open(file, OUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)};
    if(w.ok) line(&w, "unix_s,elapsed_ms,pass,step,requested_hz,actual_hz,rssi_dbm_x10\n");
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    Screen s = {.phase = 0, .remaining = 3};
    ViewPort* port = view_port_alloc();
    view_port_draw_callback_set(port, draw, &s); gui_add_view_port(gui, port, GuiLayerFullscreen);
    for(uint32_t i = 3; i > 0; i--) { s.remaining = i; view_port_update(port); furi_delay_ms(1000); }
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_270khz_async_regs);
    uint32_t start = furi_get_tick(), last_display = 0;
    s.phase = 1;
    while(w.ok && furi_get_tick() - start < CAPTURE_MS) {
        s.pass++;
        for(uint16_t step = 0; step <= 80 && w.ok; step++) {
            uint32_t requested = 863000000 + (uint32_t)step * 25000;
            if(!furi_hal_subghz_is_frequency_valid(requested)) { w.ok = false; break; }
            furi_hal_subghz_idle();
            uint32_t actual = furi_hal_subghz_set_frequency_and_path(requested);
            furi_hal_subghz_rx(); furi_delay_ms(4);
            int32_t rssi = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
            char row[96];
            snprintf(row, sizeof(row), "%lu,%lu,%lu,%u,%lu,%lu,%ld\n",
                furi_hal_rtc_get_timestamp(), furi_get_tick() - start, s.pass,
                (unsigned)step, requested, actual, rssi);
            line(&w, row);
        }
        uint32_t elapsed = furi_get_tick() - start;
        uint32_t remaining = elapsed < CAPTURE_MS ? (CAPTURE_MS - elapsed + 999) / 1000 : 0;
        s.remaining = remaining;
        if(remaining != last_display) { view_port_update(port); last_display = remaining; }
    }
    furi_hal_subghz_idle(); furi_hal_subghz_sleep();
    flush(&w); if(w.ok) storage_file_sync(file);
    s.phase = 2; view_port_update(port); furi_delay_ms(1200);
    gui_remove_view_port(gui, port); view_port_free(port);
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION); furi_record_close(RECORD_GUI);
    storage_file_close(file); storage_file_free(file); furi_record_close(RECORD_STORAGE);
    return w.ok ? 0 : -1;
}
