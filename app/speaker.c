#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <stdio.h>
#include <stdlib.h>

#define AUDIO_PATH "/ext/retekess_probe/suite_capture.alaw"
static uint8_t* audio;

static int16_t decode_alaw(uint8_t value) {
    value ^= 0x55;
    int16_t result = (value & 0x0F) << 4;
    uint8_t segment = (value >> 4) & 7;
    if(segment == 0) result += 8;
    else {
        result += 0x108;
        if(segment > 1) result <<= segment - 1;
    }
    return (value & 0x80) ? result : -result;
}

bool suite_play_capture(void) {
    audio = malloc(16000);
    if(!audio) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, AUDIO_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t size = 0;
    if(opened) {
        uint64_t length = storage_file_size(file);
        if(length && length <= 16000) {
            size = storage_file_read(file, audio, length);
            if(size != length) size = 0;
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(!size || !furi_hal_speaker_acquire(1000)) {
        free(audio);
        audio = NULL;
        return false;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t cycles = SystemCoreClock / 8000;
    furi_hal_speaker_start(6000.0f, 0.5f);
    uint32_t next = DWT->CYCCNT;
    for(size_t i = 0; i < size; i++) {
        int16_t pcm = decode_alaw(audio[i]);
        float volume = 0.5f + (float)pcm / 65536.0f;
        if(volume < 0.02f) volume = 0.02f;
        if(volume > 0.98f) volume = 0.98f;
        furi_hal_speaker_set_volume(volume);
        next += cycles;
        while((int32_t)(DWT->CYCCNT - next) < 0) {}
    }
    furi_hal_speaker_stop();
    furi_hal_speaker_release();
    free(audio);
    audio = NULL;
    return true;
}
