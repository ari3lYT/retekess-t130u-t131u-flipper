#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <storage/storage.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/drivers/cc1101_regs.h>
#include <stdio.h>
#include <string.h>

#define TAG "RetekessDemod"
#define OUT_DIR "/ext/retekess_probe"
#define OUT_FILE OUT_DIR "/last_grid.csv"
#define SAMPLE_COUNT 2048

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
    {"G100D127B542", 99976, 126953, 541667, 0x10, 0xF8, 0x2B, 0x62, 0x0C},
    {"G125D127B542", 124969, 126953, 541667, 0x10, 0x3B, 0x2C, 0x62, 0x0C},
    {"G200D127B542", 199951, 126953, 541667, 0x10, 0xF8, 0x2C, 0x62, 0x0C},
    {"G250D127B542", 249939, 126953, 541667, 0x10, 0x3B, 0x2D, 0x62, 0x0C},
    {"G300D127B812", 299927, 126953, 812500, 0x10, 0x7A, 0x0D, 0x62, 0x0E},
    {"G500D127B812", 499878, 126953, 812500, 0x10, 0x3B, 0x0E, 0x62, 0x0E},
    {"F100D127B542", 99976, 126953, 541667, 0x00, 0xF8, 0x2B, 0x62, 0x0C},
    {"F125D127B542", 124969, 126953, 541667, 0x00, 0x3B, 0x2C, 0x62, 0x0C},
    {"F200D127B542", 199951, 126953, 541667, 0x00, 0xF8, 0x2C, 0x62, 0x0C},
    {"F250D127B542", 249939, 126953, 541667, 0x00, 0x3B, 0x2D, 0x62, 0x0C},
    {"F300D127B812", 299927, 126953, 812500, 0x00, 0x7A, 0x0D, 0x62, 0x0E},
    {"F500D127B812", 499878, 126953, 812500, 0x00, 0x3B, 0x0E, 0x62, 0x0E},
    {"G250D076B542", 249939, 76172, 541667, 0x10, 0x3B, 0x2D, 0x54, 0x0C},
    {"G250D228B542", 249939, 228516, 541667, 0x10, 0x3B, 0x2D, 0x71, 0x0C},
    {"F250D076B542", 249939, 76172, 541667, 0x00, 0x3B, 0x2D, 0x54, 0x0C},
    {"F250D228B542", 249939, 228516, 541667, 0x00, 0x3B, 0x2D, 0x71, 0x0C},
    {"F4_125D127B406", 124969, 126953, 406250, 0x40, 0x3B, 0x4C, 0x62, 0x08},
    {"F4_250D254B812", 249939, 253906, 812500, 0x40, 0x3B, 0x0D, 0x72, 0x0E},
    {"MSK250B542", 249939, 0, 541667, 0x70, 0x3B, 0x2D, 0x62, 0x0C},
    {"MSK500B812", 499878, 0, 812500, 0x70, 0x3B, 0x0E, 0x62, 0x0E},
};

static uint8_t samples[SAMPLE_COUNT];
static char line[720];

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
    uint32_t start_tick = furi_get_tick();
    uint32_t start_cycle = DWT->CYCCNT;
    while(furi_get_tick() - start_tick < 20) __NOP();
    uint32_t elapsed_ms = furi_get_tick() - start_tick;
    uint32_t elapsed_cycles = DWT->CYCCNT - start_cycle;
    return elapsed_cycles / (elapsed_ms * 1000);
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
    const Candidate* candidate,
    uint32_t actual_hz,
    int32_t rssi_x10,
    uint32_t core_mhz,
    uint32_t sample_us,
    uint32_t late) {
    size_t used = snprintf(
        line,
        sizeof(line),
        "%lu,%lu,%s,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,",
        (uint32_t)candidate_id,
        (uint32_t)block,
        candidate->name,
        candidate->rate_bps,
        candidate->deviation_hz,
        candidate->bandwidth_hz,
        actual_hz,
        rssi_x10,
        core_mhz,
        sample_us,
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

int32_t retekess_demod_probe_app(void* p) {
    UNUSED(p);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, OUT_DIR);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, OUT_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        const char* header =
            "candidate,block,mode,rate_bps,deviation_hz,bandwidth_hz,actual_hz,"
            "rssi_dbm_x10,core_mhz,sample_us,late_samples,packed_gpio,ones\n";
        ok = storage_file_write(file, header, strlen(header)) == strlen(header);
    }
    uint32_t core_mhz = measure_core_mhz();
    if(core_mhz < 50 || core_mhz > 100) ok = false;
    uint8_t preset[128] = {0};
    for(size_t c = 0; c < COUNT_OF(candidates) && ok; ++c) {
        const Candidate* candidate = &candidates[c];
        ok = make_candidate_preset(preset, candidate);
        if(!ok) break;
        furi_hal_subghz_reset();
        furi_hal_subghz_load_custom_preset(preset);
        furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
        for(uint32_t requested_hz = 863950000; requested_hz <= 864000000 && ok;
            requested_hz += 50000) {
            uint32_t actual_hz = furi_hal_subghz_set_frequency_and_path(requested_hz);
            furi_hal_subghz_rx();
            furi_delay_ms(70);
            for(size_t block = 0; block < 4 && ok; ++block) {
                uint32_t late = sample_gpio(core_mhz);
                int32_t rssi_x10 = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
                ok = save_block(file, c, block, candidate, actual_hz, rssi_x10, core_mhz, 1, late);
                furi_delay_ms(10);
            }
            furi_hal_subghz_idle();
        }
    }
    furi_hal_subghz_sleep();
    if(ok) ok = storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I(TAG, "Demod probe %s", ok ? "complete" : "failed");
    return ok ? 0 : -1;
}
