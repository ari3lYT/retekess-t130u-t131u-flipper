#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_spi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_BYTES 64000u
#define CAPTURE_MS 1600u
#define HZ 823475000u
#define DIR "/ext/retekess_probe"
#define BIN_PATH DIR "/packet_capture_ch1.bin"
#define META_PATH DIR "/packet_capture_ch1.csv"

static uint8_t captured[CAPTURE_BYTES];

typedef struct {
    char phase[24];
    uint32_t actual_hz;
    uint32_t bytes;
    uint32_t overflows;
    uint32_t max_fifo;
    uint32_t spi_errors;
    uint32_t polls;
} CaptureState;

// Packet-mode test of the empirically observed 0xAA preamble and 0x9B1B marker.
// It is an RX hypothesis; no packet layout beyond that marker is assumed.
static const uint8_t preset[] = {
    0x02, 0x06, // GDO0: sync detected until end of packet
    0x03, 0x47, // FIFO threshold
    0x04, 0x9B, 0x05, 0x1B, // observed sync candidate
    0x06, 0xF2, // ignored in infinite mode
    0x07, 0x00, 0x08, 0x02, // no status/address/CRC; infinite packet mode
    0x0B, 0x0C, 0x10, 0x2D, 0x11, 0x3B,
    0x12, 0x12, // GFSK + exact 16-bit sync requirement
    0x13, 0x22, 0x14, 0xF8, 0x15, 0x62,
    0x17, 0x0C, // stay in RX after packet
    0x18, 0x18, 0x19, 0x1D, 0x1A, 0x1C,
    0x1B, 0xC7, 0x1C, 0x00, 0x1D, 0xB0,
    0x20, 0xFB, 0x21, 0xB6, 0x22, 0x10,
    0x23, 0x59, 0x2C, 0x81, 0x2D, 0x35, 0x2E, 0x09,
    0x00, 0x00,
    0xC0, 0, 0, 0, 0, 0, 0, 0,
};

static bool spi_read(const uint8_t* tx, uint8_t* rx, size_t len) {
    const FuriHalSpiBusHandle* h = &furi_hal_spi_bus_handle_subghz;
    furi_hal_spi_acquire(h);
    bool ok = furi_hal_spi_bus_trx(h, tx, rx, len, 100);
    furi_hal_spi_release(h);
    return ok;
}

static bool fifo_count(uint8_t* count, bool* overflow) {
    uint8_t tx[2] = {0xFB, 0}; // RXBYTES status register, burst read
    uint8_t rx[2] = {0};
    if(!spi_read(tx, rx, sizeof(tx))) return false;
    *count = rx[1] & 0x7F;
    *overflow = rx[1] & 0x80;
    return true;
}

static bool fifo_read(uint8_t* out, uint8_t len) {
    uint8_t tx[61] = {0xFF}; // RX FIFO burst read
    uint8_t rx[61] = {0};
    if(len > 60 || !spi_read(tx, rx, len + 1)) return false;
    memcpy(out, rx + 1, len);
    return true;
}

static void draw(Canvas* canvas, void* context) {
    CaptureState* s = context;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 13, "Retekess packet RX");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 29, s->phase);
    snprintf(line, sizeof(line), "%lu bytes / %lu polls", s->bytes, s->polls);
    canvas_draw_str(canvas, 2, 43, line);
    snprintf(line, sizeof(line), "FIFO max %lu, ovf %lu", s->max_fifo, s->overflows);
    canvas_draw_str(canvas, 2, 57, line);
}

int32_t retekess_packet_capture_app(void* p) {
    UNUSED(p);
    CaptureState s = {.phase = "Prepare T130U CH1", .actual_hz = HZ};
    Gui* gui = furi_record_open(RECORD_GUI);
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_display_backlight_enforce_on);
    ViewPort* view = view_port_alloc();
    view_port_draw_callback_set(view, draw, &s);
    gui_add_view_port(gui, view, GuiLayerFullscreen);
    furi_delay_ms(3000);

    furi_hal_subghz_idle();
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(preset);
    s.actual_hz = furi_hal_subghz_set_frequency_and_path(HZ);
    furi_hal_subghz_rx();
    strcpy(s.phase, "RX 1.6 seconds...");
    view_port_update(view);
    uint32_t start = furi_get_tick();
    uint8_t temp[60];
    while(furi_get_tick() - start < CAPTURE_MS && s.bytes < CAPTURE_BYTES - 60) {
        uint8_t n = 0;
        bool overflow = false;
        s.polls++;
        if(!fifo_count(&n, &overflow)) { s.spi_errors++; break; }
        if(n > s.max_fifo) s.max_fifo = n;
        if(overflow) {
            s.overflows++;
            furi_hal_subghz_idle();
            furi_hal_subghz_flush_rx();
            furi_hal_subghz_rx();
            continue;
        }
        if(n > 60) n = 60;
        if(n) {
            if(!fifo_read(temp, n)) { s.spi_errors++; break; }
            memcpy(captured + s.bytes, temp, n);
            s.bytes += n;
        } else {
            furi_delay_ms(1);
        }
    }
    uint32_t elapsed = furi_get_tick() - start;
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, DIR);
    File* file = storage_file_alloc(storage);
    bool bin_ok = storage_file_open(file, BIN_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(bin_ok) {
        bin_ok = storage_file_write(file, captured, s.bytes) == s.bytes;
        storage_file_close(file);
    }
    bool meta_ok = storage_file_open(file, META_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(meta_ok) {
        char line[192];
        int n = snprintf(line, sizeof(line),
            "requested_hz,actual_hz,elapsed_ms,bytes,polls,max_fifo,overflows,spi_errors,bin_ok\n"
            "%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u\n",
            HZ, s.actual_hz, elapsed, s.bytes, s.polls, s.max_fifo,
            s.overflows, s.spi_errors, bin_ok);
        if(n > 0 && (size_t)n < sizeof(line)) storage_file_write(file, line, n);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    strcpy(s.phase, bin_ok ? "RX done" : "Storage error");
    view_port_update(view);
    furi_delay_ms(4000);
    gui_remove_view_port(gui, view);
    view_port_free(view);
    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    return bin_ok && meta_ok && !s.spi_errors ? 0 : -1;
}
