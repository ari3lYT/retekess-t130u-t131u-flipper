#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>
int32_t retekess_tx_capability_probe_app(void* p) {
    UNUSED(p);
    Storage* storage=furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage,"/ext/retekess_probe");
    File* file=storage_file_alloc(storage);
    bool ok=storage_file_open(file,"/ext/retekess_probe/tx_capability.csv",FSAM_WRITE,FSOM_CREATE_ALWAYS);
    if(ok) {
        const char* h="frequency_hz,valid,tx_allowed\n";
        ok=storage_file_write(file,h,strlen(h))==strlen(h);
    }
    const uint32_t f[]={830975000,863475000,863975000,864475000};
    for(size_t i=0;ok&&i<COUNT_OF(f);i++){
        char row[64];size_t n=snprintf(row,sizeof(row),"%lu,%u,%u\n",f[i],
          furi_hal_subghz_is_frequency_valid(f[i])?1:0,
          furi_hal_subghz_is_tx_allowed(f[i])?1:0);
        ok=storage_file_write(file,row,n)==n;
    }
    if(ok)storage_file_sync(file);
    storage_file_close(file);storage_file_free(file);furi_record_close(RECORD_STORAGE);
    return ok?0:-1;
}
