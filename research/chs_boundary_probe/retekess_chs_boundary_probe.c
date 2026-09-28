#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <storage/storage.h>
#include <lib/drivers/cc1101_regs.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_COUNT 16384
#define BIT_COUNT 2048
#define OUT_FILE "/ext/retekess_probe/chs_boundary_ch18.csv"
static uint8_t samples[SAMPLE_COUNT];
static char row[700];
static const uint8_t rx_preset[] = {
    CC1101_IOCFG0,0x0D, CC1101_FIFOTHR,0x47, CC1101_PKTCTRL1,0x04,
    CC1101_PKTCTRL0,0x32, CC1101_FSCTRL1,0x0C, CC1101_MDMCFG4,0x2D,
    CC1101_MDMCFG3,0x3B, CC1101_MDMCFG2,0x10, CC1101_MDMCFG1,0x22,
    CC1101_MDMCFG0,0xF8, CC1101_DEVIATN,0x62, CC1101_MCSM0,0x18,
    CC1101_FOCCFG,0x1D, CC1101_BSCFG,0x1C, CC1101_AGCCTRL2,0xC7,
    CC1101_AGCCTRL1,0x00, CC1101_AGCCTRL0,0xB0, CC1101_WORCTRL,0xFB,
    CC1101_FREND1,0xB6, CC1101_FREND0,0x10, CC1101_FSTEST,0x59,
    CC1101_TEST2,0x81, CC1101_TEST1,0x35, CC1101_TEST0,0x09,
    0x00,0x00,0xC0,0,0,0,0,0,0,0,0
};
typedef struct { uint32_t remaining; uint32_t blocks; bool active; } Screen;
static void draw(Canvas* c, void* context) {
    const Screen* s=context; char msg[32];
    canvas_clear(c);canvas_set_font(c,FontPrimary);canvas_draw_str(c,2,12,"CH18 boundary RX");
    canvas_set_font(c,FontSecondary);
    snprintf(msg,sizeof(msg),"RECORD %lu s",s->remaining);canvas_draw_str(c,2,29,msg);
    snprintf(msg,sizeof(msg),"Blocks %lu",s->blocks);canvas_draw_str(c,2,43,msg);
    canvas_draw_str(c,2,58,s->remaining>16?"WAIT: TX CH18":s->remaining>5?"SET TX CH19 NOW":"WAIT, NO PRESS");
}
static uint32_t sample_gpio(uint32_t cycles,uint32_t* start_dwt) {
    GPIO_TypeDef* port=gpio_cc1101_g0.port;uint16_t pin=gpio_cc1101_g0.pin;
    uint32_t late=0,prior=__get_PRIMASK();__disable_irq();
    uint32_t next=DWT->CYCCNT+cycles;*start_dwt=next;
    for(size_t i=0;i<SAMPLE_COUNT;i++) {
        uint32_t now;do { now=DWT->CYCCNT; } while((int32_t)(now-next)<0);
        if(now-next>cycles/2)late++;
        samples[i]=(port->IDR&pin)!=0;next+=cycles;
    }
    __set_PRIMASK(prior);return late;
}
static uint8_t best_phase(void) {
    uint16_t hist[8]={0};
    for(size_t i=1;i<SAMPLE_COUNT;i++) if(samples[i]!=samples[i-1])hist[i&7]++;
    uint8_t best=0;
    for(uint8_t i=1;i<8;i++)if(hist[i]>hist[best])best=i;
    return (best+4)&7;
}
static bool save_block(File* file,uint32_t block,uint32_t unix_s,uint32_t dwt_start,
                       uint32_t actual_hz,int32_t rssi_x10,uint32_t late) {
    uint8_t phase=best_phase();
    size_t used=snprintf(row,sizeof(row),"%lu,%lu,%lu,%lu,%ld,%lu,%u,",
        block,unix_s,dwt_start,actual_hz,rssi_x10,late,phase);
    const char hex[]="0123456789ABCDEF";
    for(size_t i=0;i<BIT_COUNT;i+=4) {
        uint8_t nibble=0;
        for(size_t k=0;k<4;k++)nibble=(nibble<<1)|samples[phase+(i+k)*8];
        row[used++]=hex[nibble];
    }
    row[used++]='\n';return storage_file_write(file,row,used)==used;
}
int32_t retekess_chs_boundary_probe_app(void* p) {
    UNUSED(p);
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    uint32_t cycles=SystemCoreClock/2000000;
    if(cycles<20||cycles>50)return -1;
    Storage* storage=furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage,"/ext/retekess_probe");
    File* file=storage_file_alloc(storage);
    bool ok=storage_file_open(file,OUT_FILE,FSAM_WRITE,FSOM_CREATE_ALWAYS);
    if(ok){const char* h="block,unix_s,dwt_start,actual_hz,rssi_dbm_x10,late_samples,phase,packed_bits\n";
        ok=storage_file_write(file,h,strlen(h))==strlen(h);}
    Gui* gui=furi_record_open(RECORD_GUI);
    NotificationApp* note=furi_record_open(RECORD_NOTIFICATION);
    notification_message(note,&sequence_display_backlight_enforce_on);
    Screen s={.remaining=20,.blocks=0,.active=true};
    ViewPort* port=view_port_alloc();view_port_draw_callback_set(port,draw,&s);
    gui_add_view_port(gui,port,GuiLayerFullscreen);
    furi_hal_subghz_reset();furi_hal_subghz_load_custom_preset(rx_preset);
    furi_hal_gpio_init(&gpio_cc1101_g0,GpioModeInput,GpioPullNo,GpioSpeedLow);
    uint32_t actual=furi_hal_subghz_set_frequency_and_path(863475000);
    furi_hal_subghz_rx();furi_delay_ms(70);
    uint32_t start=furi_hal_rtc_get_timestamp(),last_draw=0;
    while(ok&&furi_hal_rtc_get_timestamp()-start<20) {
        uint32_t unix_s=furi_hal_rtc_get_timestamp(),dwt_start=0;
        uint32_t late=sample_gpio(cycles,&dwt_start);
        int32_t rssi=(int32_t)(furi_hal_subghz_get_rssi()*10.0f);
        ok=save_block(file,s.blocks++,unix_s,dwt_start,actual,rssi,late);
        uint32_t now=furi_hal_rtc_get_timestamp();
        if(now!=last_draw){s.remaining=20-(now-start);view_port_update(port);last_draw=now;}
    }
    furi_hal_subghz_idle();furi_hal_subghz_sleep();
    if(ok)ok=storage_file_sync(file);
    s.active=false;s.remaining=0;view_port_update(port);furi_delay_ms(1100);
    gui_remove_view_port(gui,port);view_port_free(port);
    notification_message(note,&sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);furi_record_close(RECORD_GUI);
    storage_file_close(file);storage_file_free(file);furi_record_close(RECORD_STORAGE);
    return ok?0:-1;
}
