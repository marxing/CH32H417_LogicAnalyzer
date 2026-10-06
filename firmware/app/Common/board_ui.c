#include "board_ui.h"
#include "instrument.h"
#include "board_memory.h"
#ifdef INSTRUMENT_UI_TEST
#include "ui_test_hal.h"
#else
#include "ch32h417_logic.h"
#include "debug.h"
#endif
#include <stdio.h>
#ifndef APP_BOOT_DIAGNOSTIC
#define APP_BOOT_DIAGNOSTIC 0
#endif
#define LCD_RES_PORT GPIOB
#define LCD_RES_PIN GPIO_Pin_4
#define BG 0x0841
#define WHITE 0xffff
#define CYAN 0x07ff
#define GREEN 0x07e0
#define YELLOW 0xffe0
#define GRAY 0x7bef
#define BLUE 0x1129
#define RED 0xf800
typedef struct
{
    char character;
    uint8_t column[5];
} Font5x7_t;

static const Font5x7_t font5x7[] =
{
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {'/', {0x20,0x10,0x08,0x04,0x02}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'>', {0x41,0x22,0x14,0x08,0x00}},
    {'0', {0x3e,0x51,0x49,0x45,0x3e}},
    {'1', {0x00,0x42,0x7f,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4b,0x31}},
    {'4', {0x18,0x14,0x12,0x7f,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3c,0x4a,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1e}},
    {'A', {0x7e,0x11,0x11,0x11,0x7e}},
    {'B', {0x7f,0x49,0x49,0x49,0x36}},
    {'C', {0x3e,0x41,0x41,0x41,0x22}},
    {'D', {0x7f,0x41,0x41,0x22,0x1c}},
    {'E', {0x7f,0x49,0x49,0x49,0x41}},
    {'F', {0x7f,0x09,0x09,0x09,0x01}},
    {'G', {0x3e,0x41,0x49,0x49,0x7a}},
    {'H', {0x7f,0x08,0x08,0x08,0x7f}},
    {'I', {0x00,0x41,0x7f,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3f,0x01}},
    {'K', {0x7f,0x08,0x14,0x22,0x41}},
    {'L', {0x7f,0x40,0x40,0x40,0x40}},
    {'M', {0x7f,0x02,0x0c,0x02,0x7f}},
    {'N', {0x7f,0x04,0x08,0x10,0x7f}},
    {'O', {0x3e,0x41,0x41,0x41,0x3e}},
    {'P', {0x7f,0x09,0x09,0x09,0x06}},
    {'Q', {0x3e,0x41,0x51,0x21,0x5e}},
    {'R', {0x7f,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7f,0x01,0x01}},
    {'U', {0x3f,0x40,0x40,0x40,0x3f}},
    {'V', {0x1f,0x20,0x40,0x20,0x1f}},
    {'W', {0x3f,0x40,0x38,0x40,0x3f}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}}
};


static uint16_t frame[135][240];
static volatile uint32_t ticks;
static volatile uint8_t ready;
static InstrumentKeys keys;
static uint8_t raw_keys,page=255,menu,focus,zoom,bank,history_age,confirm,dirty=1;
static uint16_t row=135;
static uint32_t last_revision,last_draw;
static int action_error;
static const char *menus[]={"LOGIC","OSCILLOSCOPE","FLASH RECORDS","RAM HISTORY","SETTINGS","DIAGNOSTICS","FORMAT FLASH"};
uint32_t Board_UI_Millis(void) { return ticks; }
static void send_byte(uint8_t d)
{
    unsigned i;
    for(i=0;i<8;++i) {
        GPIOB->BCR=GPIO_Pin_3;
        if(d&128)GPIOB->BSHR=GPIO_Pin_5; else GPIOB->BCR=GPIO_Pin_5;
        __NOP(); __NOP(); __NOP(); __NOP();
        GPIOB->BSHR=GPIO_Pin_3;
        __NOP(); __NOP(); __NOP(); __NOP(); d<<=1;
    }
}
static void lcd_command(uint8_t d)
{ GPIOB->BCR=GPIO_Pin_7|GPIO_Pin_6; send_byte(d); GPIOB->BSHR=GPIO_Pin_7|GPIO_Pin_6; }
static void lcd_data8(uint8_t d)
{ GPIOB->BCR=GPIO_Pin_7; send_byte(d); GPIOB->BSHR=GPIO_Pin_7; }
static void lcd_data16(uint16_t d) { lcd_data8(d>>8); lcd_data8(d); }
static void flush_row(void)
{
    unsigned x;
    if(row>=135)return;
    lcd_command(0x2a); lcd_data16(40); lcd_data16(279);
    lcd_command(0x2b); lcd_data16(52+row); lcd_data16(52+row); lcd_command(0x2c);
    GPIOB->BCR=GPIO_Pin_7;
    for(x=0;x<240;++x) { send_byte(frame[row][x]>>8); send_byte(frame[row][x]); }
    GPIOB->BSHR=GPIO_Pin_7; ++row;
}
static void fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t c)
{
    unsigned a,b;
    for(b=y;b<y+h&&b<135;++b)for(a=x;a<x+w&&a<240;++a)frame[b][a]=c;
}
static void text_at(unsigned x,unsigned y,const char *s,uint16_t color,uint8_t scale)
{
    for(;*s&&x+6*scale<=240;++s,x+=6*scale) {
        unsigned k,a,b; char ch=*s; const uint8_t *f=font5x7[0].column;
        if(ch>='a'&&ch<='z')ch-=32;
        for(k=0;k<sizeof(font5x7)/sizeof(font5x7[0]);++k)if(font5x7[k].character==ch){f=font5x7[k].column;break;}
        for(a=0;a<5;++a)for(b=0;b<7;++b)if(f[a]&(1u<<b))fill(x+a*scale,y+b*scale,scale,scale,color);
    }
}
static void lcd_init_controller(void)
{
    GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN);
    Delay_Ms(20u);
    GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN);
    Delay_Ms(120u);

    lcd_command(0x11u);
    Delay_Ms(120u);
    lcd_command(0x36u);
    lcd_data8(0xa0u);
    lcd_command(0x3au);
    lcd_data8(0x05u);
    lcd_command(0xb2u);
    lcd_data8(0x0cu); lcd_data8(0x0cu); lcd_data8(0x00u); lcd_data8(0x33u); lcd_data8(0x33u);
    lcd_command(0xb7u); lcd_data8(0x35u);
    lcd_command(0xbbu); lcd_data8(0x19u);
    lcd_command(0xc0u); lcd_data8(0x2cu);
    lcd_command(0xc2u); lcd_data8(0x01u);
    lcd_command(0xc3u); lcd_data8(0x12u);
    lcd_command(0xc4u); lcd_data8(0x20u);
    lcd_command(0xc6u); lcd_data8(0x0fu);
    lcd_command(0xd0u); lcd_data8(0xa4u); lcd_data8(0xa1u);
    lcd_command(0xe0u);
    lcd_data8(0xd0u); lcd_data8(0x04u); lcd_data8(0x0du); lcd_data8(0x11u);
    lcd_data8(0x13u); lcd_data8(0x2bu); lcd_data8(0x3fu); lcd_data8(0x54u);
    lcd_data8(0x4cu); lcd_data8(0x18u); lcd_data8(0x0du); lcd_data8(0x0bu);
    lcd_data8(0x1fu); lcd_data8(0x23u);
    lcd_command(0xe1u);
    lcd_data8(0xd0u); lcd_data8(0x04u); lcd_data8(0x0cu); lcd_data8(0x11u);
    lcd_data8(0x13u); lcd_data8(0x2cu); lcd_data8(0x3fu); lcd_data8(0x44u);
    lcd_data8(0x51u); lcd_data8(0x2fu); lcd_data8(0x1fu); lcd_data8(0x1fu);
    lcd_data8(0x20u); lcd_data8(0x23u);
    lcd_command(0x21u);
    lcd_command(0x29u);
    Delay_Ms(20u);
}


#if APP_BOOT_DIAGNOSTIC == 2
/* Actual LCD/key drivers, with no offline or external storage calls. */
static uint8_t test_selection,last_key;
static uint32_t test_clicks,test_holds;
static void draw(void)
{
    char b[64]; unsigned i;
    fill(0,0,240,135,BG);fill(0,0,240,20,BLUE);
    text_at(6,5,"LCD KEY TEST",WHITE,1);
    text_at(6,25,"FLASH PSRAM OFFLINE DISABLED",YELLOW,1);
    for(i=0;i<3;++i){
        unsigned y=42+i*14;
        if(i==test_selection)fill(3,y-2,234,13,BLUE);
        snprintf(b,sizeof(b),"%s TEST ITEM %u",i==test_selection?">":" ",i+1);
        text_at(8,y,b,CYAN,1);
    }
    snprintf(b,sizeof(b),"RAW %u EVENT %u USB %u",raw_keys,last_key,logic_adc_info.usb_status);
    text_at(6,86,b,WHITE,1);
    snprintf(b,sizeof(b),"L %lu R %lu P %lu",(unsigned long)keys.presses[0],(unsigned long)keys.presses[1],(unsigned long)keys.presses[2]);
    text_at(6,99,b,WHITE,1);
    snprintf(b,sizeof(b),"CLICK %lu HOLD %lu",(unsigned long)test_clicks,(unsigned long)test_holds);
    text_at(6,112,b,GREEN,1);
    text_at(6,125,"WHEEL MOVE CLICK COUNT HOLD HOME",GRAY,1);
    dirty=0;row=0;last_draw=ticks;
}
static void event(uint8_t e)
{
    last_key=e;
    if(e==KEY_PREV)test_selection=(test_selection+2)%3;
    if(e==KEY_NEXT)test_selection=(test_selection+1)%3;
    if(e==KEY_CLICK)++test_clicks;
    if(e==KEY_BACK){++test_holds;test_selection=0;}
    dirty=1;
}
#else
static const char *state_text(void)
{
    static const char *s[]={"IDLE","CAPTURING","RAM CACHE","STOPPED","SAVING","LOADING","SCANNING","FORMATTING","ERROR"};
    if(logic_adc_info.usb_trans_flag)return "USB STREAM";
    return s[instrument.state<=INST_ERROR?instrument.state:INST_ERROR];
}
static void plot(void)
{
    uint32_t span,start,x; unsigned c;
    if(!instrument.samples){text_at(24,50,"SELECT RUN TO CAPTURE",GRAY,1);return;}
    span=instrument.samples>>zoom;
    if(span<240)span=instrument.samples<240?instrument.samples:240;
    start=instrument.offset; if(start+span>instrument.samples)start=instrument.samples-span;
    for(x=0;x<240;x+=40)fill(x,24,1,68,BLUE);
    if(instrument.header.mode) {
        for(x=0;x<240;++x){
            uint32_t a=start+x*span/240,b=start+(x+1)*span/240,i;
            uint16_t lo=1023,hi=0;
            if(b<=a)b=a+1;
            for(i=a;i<b&&i<instrument.samples;++i){uint16_t v=instrument_data[i];if(v<lo)lo=v;if(v>hi)hi=v;}
            fill(x,24+lo*66/1023,1,1+(hi-lo)*66/1023,instrument.header.channel?GREEN:CYAN);
        }
    } else {
        for(c=0;c<8;++c)for(x=0;x<240;++x){
            uint32_t a=start+x*span/240,b=start+(x+1)*span/240,i;
            uint8_t lo=1,hi=0; uint16_t mask=1u<<(c+bank*8);
            if(b<=a)b=a+1;
            for(i=a;i<b&&i<instrument.samples;++i){uint8_t v=(instrument_data[i]&mask)!=0;lo&=v;hi|=v;}
            fill(x,26+c*8+(hi?0:5),1,lo==hi?1:6,c&1?GREEN:CYAN);
        }
    }
    if(instrument.header.trigger>=start&&instrument.header.trigger<start+span){
        x=(instrument.header.trigger-start)*240/span;fill(x,22,1,72,YELLOW);
    }
}
static void draw(void)
{
    char b[64]; unsigned i;
    fill(0,0,240,135,BG);fill(0,0,240,20,BLUE);fill(0,119,240,16,BLUE);
#if APP_BOOT_DIAGNOSTIC == 3
    text_at(3,5,page==255?"OFFLINE INTERNAL RAM":menus[page],WHITE,1);
#else
    text_at(3,5,page==255?"CH417 OFFLINE":menus[page],WHITE,1);
#endif
    text_at(183,5,logic_adc_info.usb_status==USB_U3_CONNECT?"USB3":logic_adc_info.usb_status==USB_U2_CONNECT?"USB2":"LOCAL",CYAN,1);
    if(page==255){
        unsigned first=menu>3?menu-3:0;
        for(i=first;i<first+5&&i<7;++i){unsigned y=24+(i-first)*18;if(i==menu)fill(2,y-2,236,17,BLUE);text_at(8,y,i==menu?">":" ",CYAN,1);text_at(24,y,menus[i],WHITE,1);}
        text_at(3,124,"WHEEL MOVE CLICK OPEN HOLD BACK",GRAY,1);
    }else if(page<2){
        static const char *a[]={"RUN","SAVE FLASH","ZOOM","PAN","BANK / CHANNEL","RATE","DEPTH / RANGE","TRIGGER"};
        plot();
        snprintf(b,sizeof(b),"%s %luK %luKHZ",state_text(),(unsigned long)(instrument.samples/1024),(unsigned long)(instrument.header.rate_hz/1000));text_at(3,94,b,GRAY,1);
        if(focus==4)snprintf(b,sizeof(b),"> %s %u",a[focus],page?instrument.settings.channel+1:bank);
        else if(focus==5)snprintf(b,sizeof(b),"> RATE %u",instrument.settings.rate);
        else if(focus==6)snprintf(b,sizeof(b),"> %s %u",page?"RANGE":"DEPTH",page?instrument.settings.range[instrument.settings.channel]:instrument.settings.depth);
        else if(focus==7)snprintf(b,sizeof(b),"> TRIGGER %s",instrument.settings.trigger==0?"OFF":instrument.settings.trigger==1?"RISE":"FALL");
        else snprintf(b,sizeof(b),"> %s X%u",a[focus],1u<<zoom);
        text_at(3,106,b,CYAN,1);
        if(instrument.header.mode&&instrument.samples){
            int32_t mv=Capture_Microvolts(instrument.average,instrument.header.range,instrument.header.zero)/1000;
            snprintf(b,sizeof(b),"CH%u R%u %ldMV NOMINAL",instrument.header.channel+1,instrument.header.range,(long)mv);
        }else snprintf(b,sizeof(b),"D%u-D%u TRIG %s",bank*8,bank*8+7,instrument.header.trigger==UINT32_MAX?"NONE":"FOUND");
        text_at(3,124,b,YELLOW,1);
    }else if(page==2||page==3){
        snprintf(b,sizeof(b),"FLASH %u RAM %u",instrument.records,instrument.history_count);text_at(8,30,b,CYAN,1);
        snprintf(b,sizeof(b),"SELECT %u",page==2?instrument.selected_record+1:history_age);text_at(8,51,b,WHITE,2);
        text_at(8,82,"WHEEL SELECT / CLICK REPLAY",GRAY,1);text_at(8,98,state_text(),GREEN,1);
        text_at(3,124,"HOLD RETURNS TO MENU",GRAY,1);
    }else if(page==4){
        InstrumentSettings *s=&instrument.settings;
        snprintf(b,sizeof(b),"CH%u RANGE%u RATE%u",s->channel+1,s->range[s->channel],s->rate);text_at(8,28,b,CYAN,1);
        snprintf(b,sizeof(b),"TRIG %s D%u",s->trigger==0?"OFF":s->trigger==1?"RISE":"FALL",s->trigger_channel);text_at(8,44,b,WHITE,1);
        snprintf(b,sizeof(b),"ADC TRIG CODE %u",s->trigger_code);text_at(8,60,b,WHITE,1);
        snprintf(b,sizeof(b),"ZERO %u NOMINAL",s->zero[s->channel][s->range[s->channel]]);text_at(8,76,b,YELLOW,1);
        {static const char *a[]={"CHANNEL","RANGE","RATE","TRIGGER EDGE","TRIGGER D0-D15","TRIGGER CODE","ZERO FROM CAPTURE","SAVE SETTINGS","SINGLE / REPEAT"};snprintf(b,sizeof(b),"> %s",a[focus]);text_at(8,100,b,CYAN,1);}
        text_at(3,124,"ZERO REQUIRES GROUNDED INPUT",YELLOW,1);
    }else if(page==5){
        snprintf(b,sizeof(b),"KEY RAW %u LOST %lu",raw_keys,(unsigned long)keys.dropped);text_at(5,25,b,CYAN,1);
        snprintf(b,sizeof(b),"L %lu R %lu P %lu",(unsigned long)keys.presses[0],(unsigned long)keys.presses[1],(unsigned long)keys.presses[2]);text_at(5,40,b,WHITE,1);
#if APP_BOOT_DIAGNOSTIC == 3
        text_at(5,55,"PSRAM DISABLED",WHITE,1);
        text_at(5,70,"FLASH DISABLED",WHITE,1);
#else
        snprintf(b,sizeof(b),"RAM %s ID %02X %02X",board_memory.ram_ok?"8M OK":board_memory.ram_probed?"NONE":"PROBE",board_memory.ram_id[0],board_memory.ram_id[1]);text_at(5,55,b,WHITE,1);
        snprintf(b,sizeof(b),"NAND %s %02X %02X",board_memory.nand_ok?"256M":board_memory.nand_probed?"NONE":"PROBE",board_memory.nand_id[0],board_memory.nand_id[1]);text_at(5,70,b,WHITE,1);
#endif
        snprintf(b,sizeof(b),"BAD %u ECC %lu",instrument.bad_blocks,(unsigned long)board_memory.corrected);text_at(5,85,b,YELLOW,1);
        snprintf(b,sizeof(b),"%s %u ERR %d",state_text(),instrument.progress,instrument.error);text_at(5,100,b,GRAY,1);
        { extern uint16_t board_cc_mv[2];
          if(logic_adc_info.adc_dma_count || logic_adc_info.adc_usb_count)
              snprintf(b,sizeof(b),"ADC D%lu U%lu FW0D",(unsigned long)logic_adc_info.adc_dma_count,
                       (unsigned long)logic_adc_info.adc_usb_count);
          else
              snprintf(b,sizeof(b),"CC %u %u MV FW0D",board_cc_mv[0],board_cc_mv[1]);
          text_at(3,124,b,GRAY,1); }
    }else{
        text_at(8,30,"ERASE ALL FLASH RECORDS",RED,1);text_at(8,50,"THIS CANNOT BE UNDONE",YELLOW,1);
        text_at(8,76,confirm?"CLICK AGAIN TO CONFIRM":"CLICK TO ARM HOLD CANCEL",WHITE,1);
        snprintf(b,sizeof(b),"%s %u",state_text(),instrument.progress);text_at(8,98,b,CYAN,1);
        text_at(3,124,"NO AUTOMATIC BOOT FORMATTING",GRAY,1);
    }
#if APP_BOOT_DIAGNOSTIC == 2
    /* Preserve the last stream counters after the PC stops acquisition so a
     * failed run can be localized without a debugger or external memory. */
    if(logic_adc_info.adc_dma_count || logic_adc_info.adc_usb_count) {
        snprintf(b,sizeof(b),"ADC DMA %lu USB %lu FW0D",
                 (unsigned long)logic_adc_info.adc_dma_count,
                 (unsigned long)logic_adc_info.adc_usb_count);
        fill(0,119,240,16,BLUE); text_at(3,124,b,YELLOW,1);
    }
#endif
    if(action_error){snprintf(b,sizeof(b),"ACTION ERROR %d",action_error);fill(0,119,240,16,BLUE);text_at(3,124,b,RED,1);}
    dirty=0;row=0;last_draw=ticks;
}
static void event(uint8_t e)
{
    InstrumentSettings *s=&instrument.settings;
    if(e==KEY_BACK){Instrument_Cancel();page=255;confirm=0;action_error=0;dirty=1;return;}
    if(page==255){
        if(e==KEY_PREV)menu=(menu+6)%7;
        if(e==KEY_NEXT)menu=(menu+1)%7;
        if(e==KEY_CLICK){page=menu;focus=0;if(page<2&&!Instrument_Busy())s->mode=page;}
    }else if(page==2||page==3){
        uint16_t count=page==2?instrument.records:instrument.history_count;
        uint16_t v=page==2?instrument.selected_record:history_age;
        if(count){
            if(e==KEY_PREV)v=(v+count-1)%count;
            if(e==KEY_NEXT)v=(v+1)%count;
            if(page==2)instrument.selected_record=v;else history_age=v;
            if(e==KEY_CLICK){action_error=page==2?Instrument_Load(v):Instrument_History(v);if(!action_error){page=instrument.header.mode;focus=2;zoom=0;}}
        }
    }else if(page<2||page==4){
        unsigned count=page==4?9:8;
        if(e==KEY_PREV)focus=(focus+count-1)%count;
        if(e==KEY_NEXT)focus=(focus+1)%count;
        if(e==KEY_CLICK){
            if(page<2){
                if(focus==0){if(instrument.state==INST_ACQUIRE)Instrument_Cancel();else{s->mode=page;action_error=Instrument_Start();}}
                else if(focus==1)action_error=Instrument_Save();
                else if(focus==2){zoom=(zoom+1)%8;instrument.offset=0;}
                else if(focus==3&&instrument.samples){instrument.offset+=(instrument.samples>>zoom)/2;if(instrument.offset>=instrument.samples)instrument.offset=0;}
                else if(!Instrument_Busy()&&!logic_adc_info.usb_trans_flag){
                    if(focus==4){if(page==0)bank^=1;else s->channel^=1;}
                    if(focus==5)s->rate=(s->rate+1)%4;
                    if(focus==6){if(page==0)s->depth^=1;else s->range[s->channel]=(s->range[s->channel]+1)%4;}
                    if(focus==7)s->trigger=(s->trigger+1)%3;
                }
            }else if(!Instrument_Busy()&&!logic_adc_info.usb_trans_flag){
                if(focus==0)s->channel^=1;
                if(focus==1)s->range[s->channel]=(s->range[s->channel]+1)%4;
                if(focus==2)s->rate=(s->rate+1)%4;
                if(focus==3)s->trigger=(s->trigger+1)%3;
                if(focus==4)s->trigger_channel=(s->trigger_channel+1)%16;
                if(focus==5){s->trigger_code=(s->trigger_code+64)&1023;Instrument_ApplyTrigger();}
                if(focus==6)action_error=Instrument_Zero();
                if(focus==7)action_error=Instrument_SaveSettings();
                if(focus==8)s->repeat^=1;
            }
        }
    }else if(page==6&&e==KEY_CLICK){
        if(confirm){action_error=Instrument_Format();confirm=0;}else confirm=1;
    }
    dirty=1;
}
#endif
void Board_UI_Tick(void)
{
    ticks+=5;if(!ready)return;
    raw_keys=((GPIOE->INDR&GPIO_Pin_4)?0:1)|((GPIOE->INDR&GPIO_Pin_3)?0:2)|((GPIOE->INDR&GPIO_Pin_5)?0:4);
    Keys_Sample(&keys,raw_keys);
}
void Board_UI_Init(void)
{
    GPIO_InitTypeDef g={0};
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA|RCC_HB2Periph_GPIOB|RCC_HB2Periph_GPIOE,ENABLE);
    GPIOB->BSHR=GPIO_Pin_7|GPIO_Pin_4|GPIO_Pin_6;GPIOA->BSHR=GPIO_Pin_8;
    g.GPIO_Mode=GPIO_Mode_Out_PP;g.GPIO_Speed=GPIO_Speed_Very_High;
    g.GPIO_Pin=GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7;GPIO_Init(GPIOB,&g);
    g.GPIO_Pin=GPIO_Pin_8;GPIO_Init(GPIOA,&g);
    g.GPIO_Pin=GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5;g.GPIO_Mode=GPIO_Mode_IPU;GPIO_Init(GPIOE,&g);
    ready=1;lcd_init_controller();draw();
    GPIOA->BCR=GPIO_Pin_8; /* Q1 is PNP: active LOW backlight. */
}
void Board_UI_Process(void)
{
    uint8_t e;
    while((e=Keys_Pop(&keys))!=KEY_NONE)event(e);
#if APP_BOOT_DIAGNOSTIC == 2
    if(ticks-last_draw>=250)dirty=1;
    if(logic_adc_info.usb_trans_flag)return;
#else
    if(last_revision!=instrument.revision){last_revision=instrument.revision;dirty=1;}
    if((page==5||Instrument_Busy())&&ticks-last_draw>=250)dirty=1;
    /* One row per loop; never delay streaming/finite acquisition to refresh LCD. */
    if(logic_adc_info.usb_trans_flag||instrument.state==INST_ACQUIRE)return;
#endif
    if(dirty&&row>=135)draw();
    flush_row();
}
