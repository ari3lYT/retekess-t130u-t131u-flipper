#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/drivers/cc1101_regs.h>
#include <stdio.h>
#include <string.h>

#define TAG "RetekessRssi"
#define OUT_DIR "/ext/retekess_probe"
#define OUT_FILE OUT_DIR "/last.csv"
#define TIME_FILE OUT_DIR "/last_time.csv"
#define NARROW_FILE OUT_DIR "/last_narrow.csv"

typedef struct {
    File* file;
    char data[512];
    size_t used;
    bool ok;
} CsvWriter;

static void csv_flush(CsvWriter* csv) {
    if(!csv->ok || !csv->used) return;
    csv->ok = storage_file_write(csv->file, csv->data, csv->used) == csv->used;
    csv->used = 0;
}

static void csv_line(CsvWriter* csv, const char* line) {
    if(!csv->ok) return;
    size_t len = strlen(line);
    if(len > sizeof(csv->data) - csv->used) csv_flush(csv);
    if(len > sizeof(csv->data) || !csv->ok) {
        csv->ok = false;
        return;
    }
    memcpy(csv->data + csv->used, line, len);
    csv->used += len;
}

static void scan_range(
    CsvWriter* csv,
    uint8_t pass,
    const char* preset,
    uint32_t start,
    uint32_t stop) {
    char line[96];
    for(uint32_t requested = start; requested <= stop && csv->ok; requested += 50000) {
        if(!furi_hal_subghz_is_frequency_valid(requested)) continue;
        furi_hal_subghz_idle();
        uint32_t actual = furi_hal_subghz_set_frequency_and_path(requested);
        furi_hal_subghz_rx();
        furi_delay_us(3000);
        float rssi = furi_hal_subghz_get_rssi();
        int32_t rssi_x10 = (int32_t)(rssi * 10.0f);
        snprintf(
            line,
            sizeof(line),
            "%lu,%u,%s,%lu,%lu,%ld\n",
            furi_get_tick(),
            pass,
            preset,
            requested,
            actual,
            rssi_x10);
        csv_line(csv, line);
    }
}

static bool make_58khz_preset(uint8_t out[128]) {
    const uint8_t* source = subghz_device_cc1101_preset_ook_270khz_async_regs;
    size_t i = 0;
    bool changed = false;
    while(i + 10 < 128 && source[i] != 0) {
        out[i] = source[i];
        out[i + 1] = source[i + 1];
        if(source[i] == CC1101_MDMCFG4) {
            // CC1101 datasheet, Table 26: CHANBW_E=3, CHANBW_M=3 => 58 kHz.
            out[i + 1] = (source[i + 1] & 0x0F) | 0xF0;
            changed = true;
        }
        i += 2;
    }
    if(!changed || i + 10 > 128) return false;
    memcpy(out + i, source + i, 10); // 00 00 terminator and 8-byte PA table
    return true;
}

static void scan_narrow(CsvWriter* csv, uint8_t pass, const char* preset) {
    char line[96];
    for(uint32_t requested = 863600000; requested <= 864400000 && csv->ok;
        requested += 25000) {
        furi_hal_subghz_idle();
        uint32_t actual = furi_hal_subghz_set_frequency_and_path(requested);
        furi_hal_subghz_rx();
        furi_delay_ms(30);
        int32_t rssi_x10 = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
        snprintf(
            line,
            sizeof(line),
            "%lu,%u,%s,%lu,%lu,%ld\n",
            furi_get_tick(),
            pass,
            preset,
            requested,
            actual,
            rssi_x10);
        csv_line(csv, line);
    }
}

int32_t retekess_rssi_probe_app(void* p) {
    UNUSED(p);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, OUT_DIR);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, OUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    CsvWriter csv = {.file = file, .used = 0, .ok = opened};
    if(!opened) {
        FURI_LOG_E(TAG, "Cannot open %s: %s", OUT_FILE, storage_file_get_error_desc(file));
    } else {
        csv_line(&csv, "tick_ms,pass,preset,requested_hz,actual_hz,rssi_dbm_x10\n");
        const uint8_t* presets[] = {
            subghz_device_cc1101_preset_ook_270khz_async_regs,
            subghz_device_cc1101_preset_ook_650khz_async_regs,
        };
        const char* names[] = {"OOK270", "OOK650"};
        for(size_t pi = 0; pi < 2 && csv.ok; ++pi) {
            furi_hal_subghz_reset();
            furi_hal_subghz_load_custom_preset(presets[pi]);
            for(uint8_t pass = 0; pass < 3 && csv.ok; ++pass) {
                scan_range(&csv, pass, names[pi], 822500000, 832500000);
                scan_range(&csv, pass, names[pi], 862500000, 865500000);
            }
        }
        csv_flush(&csv);
        storage_file_sync(file);
        FURI_LOG_I(TAG, "RSSI sweep %s", csv.ok ? "complete" : "failed");

        File* time_file = storage_file_alloc(storage);
        bool time_opened = storage_file_open(time_file, TIME_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
        CsvWriter series = {.file = time_file, .used = 0, .ok = time_opened};
        if(time_opened) {
            csv_line(&series, "tick_ms,index,preset,actual_hz,rssi_dbm_x10\n");
            furi_hal_subghz_reset();
            furi_hal_subghz_load_custom_preset(
                subghz_device_cc1101_preset_ook_270khz_async_regs);
            uint32_t actual = furi_hal_subghz_set_frequency_and_path(864000000);
            furi_hal_subghz_rx();
            furi_delay_ms(50);
            char line[96];
            for(uint32_t i = 0; i < 2000 && series.ok; ++i) {
                int32_t rssi_x10 = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
                snprintf(
                    line,
                    sizeof(line),
                    "%lu,%lu,OOK270,%lu,%ld\n",
                    furi_get_tick(),
                    i,
                    actual,
                    rssi_x10);
                csv_line(&series, line);
                furi_delay_ms(2);
            }
            csv_flush(&series);
            storage_file_sync(time_file);
            FURI_LOG_I(TAG, "RSSI time series %s", series.ok ? "complete" : "failed");
        }
        storage_file_close(time_file);
        storage_file_free(time_file);

        File* narrow_file = storage_file_alloc(storage);
        bool narrow_opened =
            storage_file_open(narrow_file, NARROW_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
        CsvWriter narrow = {.file = narrow_file, .used = 0, .ok = narrow_opened};
        uint8_t narrow_preset[128] = {0};
        narrow.ok = narrow.ok && make_58khz_preset(narrow_preset);
        if(narrow.ok) {
            csv_line(&narrow, "tick_ms,pass,preset,requested_hz,actual_hz,rssi_dbm_x10\n");
            const uint8_t* narrow_presets[] = {
                subghz_device_cc1101_preset_ook_270khz_async_regs,
                narrow_preset,
            };
            const char* narrow_names[] = {"OOK270", "OOK58"};
            for(size_t pi = 0; pi < 2 && narrow.ok; ++pi) {
                furi_hal_subghz_reset();
                furi_hal_subghz_load_custom_preset(narrow_presets[pi]);
                for(uint8_t pass = 0; pass < 5 && narrow.ok; ++pass) {
                    scan_narrow(&narrow, pass, narrow_names[pi]);
                }
            }
            csv_flush(&narrow);
            storage_file_sync(narrow_file);
            FURI_LOG_I(TAG, "Narrow sweep %s", narrow.ok ? "complete" : "failed");
        }
        storage_file_close(narrow_file);
        storage_file_free(narrow_file);
        furi_hal_subghz_idle();
        furi_hal_subghz_sleep();
        csv.ok = csv.ok && series.ok && narrow.ok;
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return csv.ok ? 0 : -1;
}
