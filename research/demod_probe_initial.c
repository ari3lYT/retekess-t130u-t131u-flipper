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
#define OUT_FILE OUT_DIR "/last_demod.csv"
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

// Register values calculated from CC1101 datasheet equations, not presumed
// T130U settings. Every row is an experimental receiver hypothesis.
static const Candidate candidates[] = {
    {"OOK_control", 3794, 0, 270833, 0, 0, 0, 0, 0},
    {"GFSK_100k_102k", 99976, 101563, 541667, 0x10, 0xF8, 0x2B, 0x60, 0x06},
    {"GFSK_250k_127k", 249939, 126953, 541667, 0x10, 0x3B, 0x2D, 0x62, 0x06},
    {"GFSK_300k_127k", 299927, 126953, 812500, 0x10, 0x7A, 0x0D, 0x62, 0x0E},
    {"FSK_250k_127k", 249939, 126953, 541667, 0x00, 0x3B, 0x2D, 0x62, 0x06},
};

static uint8_t samples[SAMPLE_COUNT];
static char line[720];

static bool make_candidate_preset(uint8_t out[128], const Candidate* candidate) {
    const uint8_t* source = subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs;
    size_t i = 0;
    uint8_t flags = 0;
    while(i + 10 < 128 && source[i]) {
        uint8_t address = source[i];
        uint8_t value = source[i + 1];
        switch(address) {
        case CC1101_MDMCFG2:
            value = candidate->mdmcfg2;
            flags |= 1;
            break;
        case CC1101_MDMCFG3:
            value = candidate->mdmcfg3;
            flags |= 2;
            break;
        case CC1101_MDMCFG4:
            value = candidate->mdmcfg4;
            flags |= 4;
            break;
        case CC1101_DEVIATN:
            value = candidate->deviatn;
            flags |= 8;
            break;
        case CC1101_FSCTRL1:
            value = candidate->fsctrl1;
            flags |= 16;
            break;
        default:
            break;
        }
        out[i] = address;
        out[i + 1] = value;
        i += 2;
    }
    if(i + 10 > 128 || flags != 31) return false;
    memcpy(out + i, source + i, 10);
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
        const uint8_t* selected = subghz_device_cc1101_preset_ook_270khz_async_regs;
        if(c != 0) {
            ok = make_candidate_preset(preset, candidate);
            selected = preset;
        }
        if(!ok) break;
        furi_hal_subghz_reset();
        furi_hal_subghz_load_custom_preset(selected);
        uint32_t actual_hz = furi_hal_subghz_set_frequency_and_path(864000000);
        furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
        furi_hal_subghz_rx();
        furi_delay_ms(70);
        int32_t rssi_x10 = (int32_t)(furi_hal_subghz_get_rssi() * 10.0f);
        for(size_t block = 0; block < 8 && ok; ++block) {
            uint32_t sample_us = block % 2 ? 2 : 1;
            uint32_t late = sample_gpio(core_mhz * sample_us);
            ok = save_block(
                file, c, block, candidate, actual_hz, rssi_x10, core_mhz, sample_us, late);
            furi_delay_ms(10);
        }
        furi_hal_subghz_idle();
    }
    furi_hal_subghz_sleep();
    if(ok) ok = storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I(TAG, "Demod probe %s", ok ? "complete" : "failed");
    return ok ? 0 : -1;
}
