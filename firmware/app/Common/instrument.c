#include "instrument.h"
#include "board_memory.h"
#ifdef INSTRUMENT_HOST_TEST
#include "instrument_test_hal.h"
#else
#include "board_ui.h"
#include "ch32h417_logic.h"
#include "ch32h417_uhsif.h"
#include "ch32h417_usbss_device.h"
#endif
#include <string.h>
Instrument instrument;
uint16_t instrument_data[CAPTURE_MAX_BYTES/2];
volatile uint8_t adc_snapshot;
extern uint16_t ADC_BufA[],ADC_BufB[];
extern void UHSIF_Clock_Set(uint32_t);
static volatile uint8_t logic_local, logic_done, line_count[2], block_mask;
static volatile uint32_t block_length[4];
static uint8_t wanted_blocks, history_next, history_valid[128];
static CaptureHeader history[128];
static uint16_t record_blocks[NAND_BLOCKS], free_block, scan_block, job_block;
static uint8_t block_state[NAND_BLOCKS]; /* 0 unknown,1 free,2 committed,3 bad/dirty */
static uint8_t page_buf[2048] __attribute__((aligned(4)));
static uint8_t verify_buf[2048] __attribute__((aligned(4)));
static uint32_t position, crc, started, sequence;
static const uint32_t fs_magic=0x31534644u;
typedef struct { uint32_t magic,generation; InstrumentSettings value; uint32_t crc; } SettingsRecord;
static uint32_t settings_generation, last_complete;
static uint8_t settings_block;
static int write_verified(uint32_t page,const void *data);
static int settings_valid(const SettingsRecord *p)
{
    unsigned c,r;
    if(p->magic!=0x31474643u || p->value.mode>1 || p->value.channel>1 ||
       p->value.rate>3 || p->value.depth>1 || p->value.trigger>2 ||
       p->value.trigger_channel>15 || p->value.trigger_code>1023 || p->value.repeat>1 ||
       p->crc!=~Capture_CRC(~0u,p,offsetof(SettingsRecord,crc))) return 0;
    for(c=0;c<2;++c) {
        if(p->value.range[c]>3) return 0;
        for(r=0;r<4;++r) if(p->value.zero[c][r]>1023) return 0;
    }
    return 1;
}
void Instrument_ApplyTrigger(void)
{
#ifndef INSTRUMENT_HOST_TEST
    DAC_SetChannel2Data(DAC_Align_12b_R,(uint32_t)instrument.settings.trigger_code*4095u/1023u);
#endif
}
int Instrument_SaveSettings(void)
{
    SettingsRecord rec={0};
    uint8_t target=settings_block==1?2:1;
    int r;
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag || !instrument.formatted) return -10;
    if((r=NAND_BadBlock(target))) return r<0?r:MEM_BAD;
    rec.magic=0x31474643u; rec.generation=settings_generation+1; rec.value=instrument.settings;
    rec.value.repeat=0; /* Never start acquisition automatically on power-up. */
    rec.crc=~Capture_CRC(~0u,&rec,offsetof(SettingsRecord,crc));
    if((r=NAND_Erase(target))) return r;
    memset(page_buf,0xff,2048); memcpy(page_buf,&rec,sizeof(rec));
    if((r=write_verified((uint32_t)target*64,page_buf))) return r;
    settings_block=target; settings_generation=rec.generation; return 0;
}
static void changed(void) { ++instrument.revision; }
static int fail(int e) { instrument.error=e; instrument.state=INST_ERROR; changed(); return e; }
static void finish_header(CaptureHeader *h) { h->header_crc=~Capture_CRC(~0u,h,offsetof(CaptureHeader,header_crc)); }
static void statistics(void)
{
    uint32_t i,sum=0;
    instrument.samples=instrument.header.bytes/2;
    instrument.minimum=1023; instrument.maximum=0;
    if(instrument.header.mode) {
        for(i=0;i<instrument.samples;++i) {
            uint16_t v=instrument_data[i]&1023u;
            instrument_data[i]=v; sum+=v;
            if(v<instrument.minimum) instrument.minimum=v;
            if(v>instrument.maximum) instrument.maximum=v;
        }
        instrument.average=sum/instrument.samples;
    }
}
int Instrument_Busy(void)
{
    uint8_t s=instrument.state;
    return s!=INST_IDLE && s!=INST_READY && s!=INST_ERROR;
}
void Instrument_Init(void)
{
    unsigned c,r;
    memset(&instrument,0,sizeof(instrument));
    memset(block_state,0,sizeof(block_state)); sequence=0;
    instrument.settings.depth=1; instrument.settings.rate=2;
    instrument.settings.trigger_code=512;
    memset(history_valid,0,sizeof(history_valid)); history_next=0;
    settings_generation=0; settings_block=0;
    for(c=0;c<2;++c) for(r=0;r<4;++r) instrument.settings.zero[c][r]=512;
    Memory_Init();
    if(board_memory.nand_ok) {
        if(!NAND_Read(0,0,page_buf,2048) && !memcmp(page_buf,&fs_magic,4) &&
           *(uint32_t*)(page_buf+4)==~fs_magic) {
            unsigned b;
            for(b=1;b<=2;++b) {
                SettingsRecord rec;
                if(!NAND_BadBlock(b) && !NAND_Read(b*64,0,&rec,sizeof(rec)) && settings_valid(&rec) && rec.generation>=settings_generation) {
                    instrument.settings=rec.value; settings_generation=rec.generation; settings_block=b;
                }
            }
            instrument.formatted=1; scan_block=3; instrument.state=INST_MOUNT;
        } else instrument.mounted=1;
    } else instrument.mounted=1;
    {
#ifndef INSTRUMENT_HOST_TEST
        GPIO_InitTypeDef g={0}; DAC_InitTypeDef d={0};
        RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA,ENABLE);
        RCC_HB1PeriphClockCmd(RCC_HB1Periph_DAC,ENABLE);
        g.GPIO_Pin=GPIO_Pin_5; g.GPIO_Mode=GPIO_Mode_AIN; GPIO_Init(GPIOA,&g);
        g.GPIO_Pin=GPIO_Pin_2|GPIO_Pin_3; g.GPIO_Mode=GPIO_Mode_IN_FLOATING; GPIO_Init(GPIOA,&g);
        d.DAC_Trigger=DAC_Trigger_None; d.DAC_OutputBuffer=DAC_OutputBuffer_Enable;
        DAC_Init(DAC_Channel_2,&d); DAC_Cmd(DAC_Channel_2,ENABLE); Instrument_ApplyTrigger();
#endif
    }
    changed();
}
int Instrument_Start(void)
{
    static const uint8_t div[4]={40,20,8,4};
    static const uint8_t adcdiv[4]={63,31,15,3};
    InstrumentSettings *s=&instrument.settings;
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag) return -10;
    instrument.error=0; instrument.samples=0; instrument.offset=0;
    memset(&instrument.header,0,sizeof(instrument.header));
    instrument.header.magic=CAPTURE_MAGIC; instrument.header.version=1;
    instrument.header.mode=s->mode; instrument.header.channel=s->channel;
    instrument.header.range=s->range[s->channel]; instrument.header.zero=s->zero[s->channel][s->range[s->channel]];
    instrument.header.bits=s->mode ? 10 : 16; instrument.header.trigger=UINT32_MAX;
    instrument.header.sequence=++sequence;
    Instrument_ApplyTrigger();
    logic_local=logic_done=block_mask=0; line_count[0]=line_count[1]=0;
    UHSIF_Start(DISABLE); HSADC_Function_Stop();
    instrument.state=INST_ACQUIRE;
    if(s->mode) {
        logic_adc_info.adc_channel=s->channel; logic_adc_info.adc_bit=ADC_GET_WIDTH10;
        logic_adc_info.adc_div=adcdiv[s->rate&3];
        ADC_AFE_SetRange(s->channel,s->range[s->channel]);
        adc_snapshot=1;
        instrument.header.bytes=ADC_BUF_LEN;
        instrument.header.rate_hz=80000000u/(adcdiv[s->rate&3]+1u);
        HSADC_Function_Start();
    } else {
        UHSIF_Clock_Set(RCC_PLLMUL16);
        wanted_blocks=s->depth==0?2:4;
        instrument.header.bytes=(uint32_t)wanted_blocks*16384u;
        instrument.header.rate_hz=400000000u/div[s->rate&3];
        UHSIF_Set_Para(16,div[s->rate&3]-1u); /* WCH encoded divider = divisor-1 */
        logic_local=1; UHSIF_Start(ENABLE);
    }
    started=Board_UI_Millis(); changed(); return 0;
}
uint8_t Instrument_LogicIRQ(uint8_t line,uint32_t length)
{
    uint8_t slot,index;
    if(!logic_local) return 0;
    if(!length) return 1;
    slot=line_count[line]++; index=slot*2+line;
    if(index<wanted_blocks) { block_length[index]=length; block_mask|=1u<<index; }
    if((block_mask&((1u<<wanted_blocks)-1u))==((1u<<wanted_blocks)-1u)) {
        UHSIF_Start(DISABLE); logic_done=1;
    }
    /* No recycling: the original eight descriptors/line retain their data. */
    return 1;
}
void Instrument_Cancel(void)
{
    instrument.settings.repeat=0;
    if(instrument.state==INST_ACQUIRE) {
        UHSIF_Start(DISABLE); HSADC_Function_Stop(); adc_snapshot=0; logic_local=0;
        UHSIF_Para_Init(); instrument.samples=0;
        instrument.state=INST_IDLE; changed();
    }
    /* Do not interrupt NAND program/format. UI remains responsive; progress shown. */
}
static int write_verified(uint32_t page,const void *data)
{
    int r=NAND_Program(page,data);
    if(!r) r=NAND_Read(page,0,verify_buf,2048);
    if(!r && memcmp(data,verify_buf,2048)) r=MEM_PROGRAM;
    return r;
}
int Instrument_Save(void)
{
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag || !instrument.samples) return -10;
    if(!board_memory.nand_ok || !instrument.formatted || !instrument.mounted) return -11;
    instrument.settings.repeat=0;
    for(free_block=3;free_block<NAND_BLOCKS;++free_block) if(block_state[free_block]==1) break;
    if(free_block==NAND_BLOCKS) return -12;
    job_block=free_block; block_state[job_block]=3;
    /* Page 0 is a non-FF allocation marker, even for an all-high digital capture.
     * Interrupted saves cannot be mistaken for erased space after reboot. */
    memset(page_buf,0xff,2048); memcpy(page_buf,&instrument.header,sizeof(CaptureHeader));
    {
        int r=write_verified((uint32_t)job_block*64,page_buf);
        if(r) return fail(r);
    }
    position=0; instrument.progress=0; instrument.state=INST_SAVE; changed(); return 0;
}
int Instrument_Load(uint16_t index)
{
    CaptureHeader h;
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag || index>=instrument.records) return -10;
    job_block=record_blocks[index];
    instrument.settings.repeat=0;
    if(NAND_Read((uint32_t)job_block*64+63,0,&h,sizeof(h)) || !Capture_HeaderValid(&h)) return -13;
    instrument.header=h; position=0; crc=~0u;
    instrument.samples=0; instrument.selected_record=index; instrument.state=INST_LOAD;
    changed(); return 0;
}
int Instrument_History(uint8_t age)
{
    uint8_t slot=(history_next-1u-age)&127u;
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag || !history_valid[slot]) return -10;
    /* Read in main-loop pieces, same as NAND replay; flag separates sources. */
    job_block=slot; instrument.header=history[slot]; instrument.header.flags|=0x8000;
    instrument.settings.repeat=0;
    position=0; crc=~0u; instrument.samples=0; instrument.state=INST_LOAD; changed(); return 0;
}
int Instrument_Format(void)
{
    if(!board_memory.nand_ok) return MEM_ID;
    if(Instrument_Busy() || logic_adc_info.usb_trans_flag) return -10;
    /* Invalidate superblock FIRST; commit it LAST. Power loss means unformatted. */
    if(NAND_Erase(0)) return fail(MEM_ERASE);
    instrument.formatted=instrument.mounted=0; instrument.records=0; instrument.bad_blocks=0;
    settings_generation=0; settings_block=0; instrument.settings.repeat=0;
    memset(block_state,0,sizeof(block_state)); scan_block=1;
    instrument.state=INST_FORMAT; changed(); return 0;
}
int Instrument_Zero(void)
{
    if(Instrument_Busy() || !instrument.samples || !instrument.header.mode) return -10;
    if(instrument.minimum<16 || instrument.maximum>1007 || instrument.maximum-instrument.minimum>20) return -14;
    instrument.settings.zero[instrument.header.channel][instrument.header.range]=instrument.average;
    instrument.header.zero=instrument.average; finish_header(&instrument.header); changed(); return 0;
}
void Instrument_Process(void)
{
    int r; uint32_t n;
    if(logic_adc_info.usb_trans_flag) return;
    switch(instrument.state) {
    case INST_ACQUIRE:
        if(instrument.header.mode ? adc_snapshot!=2 : !logic_done) {
            if(Board_UI_Millis()-started>1000) { Instrument_Cancel(); fail(-15); }
            return;
        }
        if(instrument.header.mode) {
            const void *p=(logic_adc_info.adc_ready_mask&1)?ADC_BufA:ADC_BufB;
            memcpy(instrument_data,p,instrument.header.bytes);
            HSADC_Function_Stop(); adc_snapshot=0;
        } else {
            unsigned b;
            logic_local=0;
            for(b=0;b<wanted_blocks;++b) {
                const uint8_t *p=(b&1)?USBSS_EP2_Tx_Th1_Buf:USBSS_EP2_Tx_Th0_Buf;
                if(block_length[b]!=16384) { UHSIF_Para_Init(); fail(-16); return; }
                memcpy((uint8_t*)instrument_data+b*16384,p+(b/2)*16384,16384);
            }
            UHSIF_Para_Init();
        }
        statistics();
        if(instrument.settings.trigger) {
            /* AFE is inverting: input rising corresponds to ADC code falling. */
            instrument.header.trigger=Capture_FindEdge(instrument_data,instrument.samples,
                1u<<instrument.settings.trigger_channel,instrument.settings.trigger_code,
                instrument.header.mode,(instrument.settings.trigger==2)^instrument.header.mode);
        }
        instrument.header.data_crc=~Capture_CRC(~0u,instrument_data,instrument.header.bytes);
        finish_header(&instrument.header);
        position=0; instrument.state=board_memory.ram_ok ? INST_CACHE : INST_READY;
        last_complete=Board_UI_Millis(); changed();
        break;
    case INST_CACHE:
        n=instrument.header.bytes-position; if(n>256) n=256;
        r=PSRAM_Write((uint32_t)history_next*CAPTURE_MAX_BYTES+position,(uint8_t*)instrument_data+position,n);
        if(r) { board_memory.ram_ok=0; instrument.error=r; instrument.state=INST_READY; changed(); break; }
        position+=n; instrument.progress=position*100/instrument.header.bytes;
        if(position==instrument.header.bytes) {
            history[history_next]=instrument.header; history_valid[history_next]=1;
            history_next=(history_next+1)&127;
            if(instrument.history_count<128) ++instrument.history_count;
            instrument.state=INST_READY; last_complete=Board_UI_Millis(); changed();
        }
        break;
    case INST_MOUNT:
    case INST_FORMAT:
        if(scan_block<NAND_BLOCKS) {
            uint32_t row=(uint32_t)scan_block*64; CaptureHeader h;
            r=NAND_BadBlock(scan_block);
            if(r<0) { fail(r); break; }
            if(r) { block_state[scan_block]=3; ++instrument.bad_blocks; }
            else if(instrument.state==INST_FORMAT) {
                r=NAND_Erase(scan_block);
                block_state[scan_block]=r||scan_block<3?3:1;
                if(r) ++instrument.bad_blocks;
            } else {
                r=NAND_Read(row+63,0,&h,sizeof(h));
                if(!r && Capture_HeaderValid(&h)) {
                    block_state[scan_block]=2; record_blocks[instrument.records++]=scan_block;
                    if(h.sequence>sequence) sequence=h.sequence;
                } else {
                    uint32_t first=0;
                    int e=NAND_Read(row,0,&first,4);
                    block_state[scan_block]=(!r && !e && h.magic==~0u && first==~0u)?1:3;
                }
            }
            ++scan_block; instrument.progress=scan_block*100u/NAND_BLOCKS;
        } else {
            if(instrument.state==INST_FORMAT) {
                memset(page_buf,0xff,sizeof(page_buf)); memcpy(page_buf,&fs_magic,4);
                *(uint32_t*)(page_buf+4)=~fs_magic;
                if((r=write_verified(0,page_buf))) { fail(r); break; }
            }
            instrument.mounted=instrument.formatted=1;
            instrument.state=instrument.samples?INST_READY:INST_IDLE; changed();
        }
        break;
    case INST_SAVE:
        if(position<instrument.header.bytes) {
            memset(page_buf,0xff,2048); n=instrument.header.bytes-position; if(n>2048) n=2048;
            memcpy(page_buf,(uint8_t*)instrument_data+position,n);
            if((r=write_verified((uint32_t)job_block*64+1+position/2048,page_buf))) { fail(r); break; }
            position+=n; instrument.progress=position*100/instrument.header.bytes;
        } else {
            memset(page_buf,0xff,2048); memcpy(page_buf,&instrument.header,sizeof(CaptureHeader));
            if((r=write_verified((uint32_t)job_block*64+63,page_buf))) { fail(r); break; }
            block_state[job_block]=2; record_blocks[instrument.records++]=job_block;
            instrument.state=INST_READY; last_complete=Board_UI_Millis(); changed();
        }
        break;
    case INST_LOAD:
        n=instrument.header.bytes-position; if(n>2048) n=2048;
        if(instrument.header.flags&0x8000) {
            if(n>256) n=256;
            r=PSRAM_Read((uint32_t)job_block*CAPTURE_MAX_BYTES+position,(uint8_t*)instrument_data+position,n);
        } else r=NAND_Read((uint32_t)job_block*64+1+position/2048,0,(uint8_t*)instrument_data+position,n);
        if(r) { fail(r); break; }
        crc=Capture_CRC(crc,(uint8_t*)instrument_data+position,n); position+=n;
        instrument.progress=position*100/instrument.header.bytes;
        if(position==instrument.header.bytes) {
            if(~crc!=instrument.header.data_crc) { fail(-17); break; }
            instrument.header.flags&=~0x8000; finish_header(&instrument.header);
            statistics(); instrument.offset=0; instrument.state=INST_READY; changed();
        }
        break;
    case INST_READY:
        if(instrument.settings.repeat && Board_UI_Millis()-last_complete>=500) {
            last_complete=Board_UI_Millis(); Instrument_Start();
        }
        break;
    default: break;
    }
}
/* Extension C0=state, C1=header chunk, C2=data chunk. Read only: no new flash erase
 * over USB. Requests use LE32 byte offset in payload; responses are 32 bytes,
 * [same cmd, 1+payload length, status, up to 29 bytes]. */
uint8_t Instrument_Command(uint8_t cmd,const uint8_t *req,uint8_t *out)
{
    uint32_t off,n; const uint8_t *p;
    if(cmd<0xc0 || cmd>0xc2) return 0;
    memset(out,0,32); out[0]=cmd; out[1]=1;
    if(cmd==0xc0) {
        out[1]=9; out[3]=instrument.state; out[4]=board_memory.ram_ok; out[5]=board_memory.nand_ok;
        out[6]=instrument.formatted; memcpy(out+7,&instrument.samples,4); return 1;
    }
    if(Instrument_Busy() || !instrument.samples || logic_adc_info.usb_trans_flag) { out[2]=1; return 1; }
    /* Do not trust capture metadata as the physical buffer capacity. */
    if(cmd==0xc2 && (instrument.header.bytes==0 ||
       instrument.header.bytes>sizeof(instrument_data) ||
       (instrument.header.bytes&1u))) { out[2]=2; return 1; }
    memcpy(&off,req,4);
    n=cmd==0xc1?sizeof(CaptureHeader):instrument.header.bytes;
    if(off>=n) { out[2]=2; return 1; }
    n-=off; if(n>29) n=29;
    p=cmd==0xc1?(const uint8_t*)&instrument.header:(const uint8_t*)instrument_data;
    memcpy(out+3,p+off,n); out[1]=n+1; return 1;
}
