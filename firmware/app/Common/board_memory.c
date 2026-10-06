#include "board_memory.h"
#include "ch32h417.h"
#include "debug.h"
#include <string.h>
MemoryInfo board_memory;
#if defined(APP_BOOT_DIAGNOSTIC) && APP_BOOT_DIAGNOSTIC == 3
/* Offline isolation stage: never access external pins or QSPI registers. */
void Memory_Init(void) { memset(&board_memory,0,sizeof(board_memory)); }
int PSRAM_Read(uint32_t a,void *p,uint32_t n) { (void)a;(void)p;(void)n;return MEM_ID; }
int PSRAM_Write(uint32_t a,const void *p,uint32_t n) { (void)a;(void)p;(void)n;return MEM_ID; }
int PSRAM_SelfTest(void) { return MEM_ID; }
int NAND_Read(uint32_t a,uint16_t c,void *p,uint16_t n) { (void)a;(void)c;(void)p;(void)n;return MEM_ID; }
int NAND_Program(uint32_t a,const void *p) { (void)a;(void)p;return MEM_ID; }
int NAND_Erase(uint16_t b) { (void)b;return MEM_ID; }
int NAND_BadBlock(uint16_t b) { (void)b;return MEM_ID; }
#else
/* Rev 2026-09-12: QSPI1 PB2 AF9, PF8/9 AF10; GPIO CS PC4/PC5.
 * SPI x1 baseline: PF6/PF7 held HIGH (NAND HOLD#/WP#). Never AF PB6: LCD DC!
 * No memory mapped mode: PSRAM and NAND share the wires, not an address space.
 * Both devices are optional. A missing device is a normal degraded mode. */
static void qspi_configure(void)
{
    QSPI_InitTypeDef q={0};
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_QSPI1,ENABLE);
    q.QSPI_Prescaler=15;
    q.QSPI_CKMode=QSPI_CKMode_Mode0;
    q.QSPI_CSHTime=QSPI_CSHTime_8Cycle;
    q.QSPI_FSize=23;
    QSPI_Init(QSPI1,&q);
    QSPI_SetFIFOThreshold(QSPI1,0);
    QSPI_Cmd(QSPI1,ENABLE);
}

static int qspi_wait_idle(uint32_t budget)
{
    while((QSPI1->SR&QSPI_FLAG_BUSY) && budget) --budget;
    return budget!=0;
}

static void qspi_recover(void)
{
    /* Always release both optional devices before resetting the controller. */
    GPIOC->BSHR=GPIO_Pin_4|GPIO_Pin_5;
    QSPI_AbortRequest(QSPI1);
    if(!qspi_wait_idle(20000)) {
        RCC_HB1PeriphResetCmd(RCC_HB1Periph_QSPI1,ENABLE);
        RCC_HB1PeriphResetCmd(RCC_HB1Periph_QSPI1,DISABLE);
        qspi_configure();
    }
    QSPI_ClearFlag(QSPI1,QSPI_FLAG_TC|QSPI_FLAG_TE|QSPI_FLAG_TO);
}

static int xfer(uint8_t ram,uint8_t cmd,uint8_t abits,uint32_t addr,
                uint8_t dummy,void *data,uint32_t n,uint8_t read)
{
    QSPI_ComConfig_InitTypeDef c={0};
    uint32_t i=0, budget=200000, saved=0, cycle_start=0, cycle_now=0;
    uint16_t cs=ram ? GPIO_Pin_4 : GPIO_Pin_5;
    uint8_t *p=data;
    int rc=MEM_OK;
    if(n && !data) return MEM_BOUNDS;
    if(!qspi_wait_idle(20000)) qspi_recover();
    if(!qspi_wait_idle(20000)) {
        ++board_memory.timeouts;
        board_memory.bus_ok=0;
        board_memory.last_error=MEM_TIMEOUT;
        return MEM_TIMEOUT;
    }
    QSPI1->CR=(QSPI1->CR&0x00ffffffu)|(((SystemClock+24999999u)/25000000u-1u)<<24);
    c.QSPI_ComConfig_IMode=QSPI_ComConfig_IMode_1Line;
    c.QSPI_ComConfig_Ins=cmd;
    c.QSPI_ComConfig_ADMode=abits ? QSPI_ComConfig_ADMode_1Line : 0;
    c.QSPI_ComConfig_ADSize=abits==24 ? QSPI_ComConfig_ADSize_24bit : abits==16 ? QSPI_ComConfig_ADSize_16bit : 0;
    c.QSPI_ComConfig_DMode=n ? QSPI_ComConfig_DMode_1Line : 0;
    c.QSPI_ComConfig_DummyCycles=dummy;
    c.QSPI_ComConfig_FMode=read ? QSPI_ComConfig_FMode_Indirect_Read : QSPI_ComConfig_FMode_Indirect_Write;
    QSPI_ClearFlag(QSPI1,QSPI_FLAG_TC|QSPI_FLAG_TE|QSPI_FLAG_TO);
    QSPI_ComConfig_Init(QSPI1,&c);
    if(abits) QSPI_SetAddress(QSPI1,addr);
    /* QSPI_SetDataLength() writes n-1. Calling it with zero used to write
     * 0xffffffff and could leave command-only probes BUSY indefinitely. */
    if(n) QSPI_SetDataLength(QSPI1,n);
    /* PSRAM CE must not span an ISR. Eight-byte x1 bursts take 96 clocks;
     * saved QingKe CSR 0x800, not mstatus (which does not mask WCH interrupts). */
    if(ram) {
        __asm__ volatile("csrr %0, 0x800":"=r"(saved)); __disable_irq();
        __asm__ volatile("csrr %0, mcycle":"=r"(cycle_start));
    }
    GPIOC->BCR=cs;
    QSPI_Start(QSPI1);
    while(i<n && budget) {
        --budget;
        if(ram) {
            __asm__ volatile("csrr %0, mcycle":"=r"(cycle_now));
            if(cycle_now-cycle_start>SystemCoreClock/1000000u*6u) { budget=0; break; }
        }
        uint32_t level=QSPI_GetFIFOLevel(QSPI1);
        if(read ? level!=0 : level<16) {
            if(read) p[i]=QSPI_ReceiveData8(QSPI1); else QSPI_SendData8(QSPI1,p[i]);
            ++i;
        }
        if(QSPI1->SR&QSPI_FLAG_TE) { rc=MEM_TIMEOUT; break; }
    }
    if(i!=n) rc=MEM_TIMEOUT;
    while(!(QSPI1->SR&QSPI_FLAG_TC) && budget) {
        --budget;
        if(ram) {
            __asm__ volatile("csrr %0, mcycle":"=r"(cycle_now));
            if(cycle_now-cycle_start>SystemCoreClock/1000000u*6u) { budget=0; break; }
        }
    }
    if(!budget) rc=MEM_TIMEOUT;
    /* Hold after the last falling edge (tCHD), then release refresh. */
    for(volatile unsigned hold=0;hold<12;++hold) __NOP();
    GPIOC->BSHR=cs;
    if(ram) __asm__ volatile("csrw 0x800, %0"::"r"(saved):"memory");
    if(rc) { ++board_memory.timeouts; qspi_recover(); }
    QSPI_ClearFlag(QSPI1,QSPI_FLAG_TC|QSPI_FLAG_TE|QSPI_FLAG_TO);
    Delay_Us(1); /* both chips CS-high recovery, outside the critical region */
    if(rc) board_memory.last_error=rc;
    return rc;
}
static int feature(uint8_t reg,uint8_t *value,uint8_t read)
{ return xfer(0,read?0x0f:0x1f,8,reg,0,value,1,read); }
static int ready(uint32_t limit,uint8_t *status)
{
    do {
        int rc=feature(0xc0,status,1); if(rc) return rc;
        if(!(*status&1)) return 0;
        Delay_Us(10);
    } while(limit--);
    board_memory.last_error=MEM_TIMEOUT; return MEM_TIMEOUT;
}
static int wren(void)
{
    uint8_t s;
    int rc=xfer(0,6,0,0,0,0,0,0); if(rc) return rc;
    rc=feature(0xc0,&s,1); return rc ? rc : (s&2 ? 0 : MEM_PROGRAM);
}
void Memory_Init(void)
{
    GPIO_InitTypeDef g={0}; uint8_t v, id2[8]; int r;
    memset(&board_memory,0,sizeof(board_memory));
    memset(board_memory.ram_id,0xff,sizeof(board_memory.ram_id));
    memset(board_memory.nand_id,0xff,sizeof(board_memory.nand_id));
    board_memory.bus_ok=1;
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB|RCC_HB2Periph_GPIOC|RCC_HB2Periph_GPIOF|RCC_HB2Periph_AFIO,ENABLE);
    GPIOC->BSHR=GPIO_Pin_4|GPIO_Pin_5;
    g.GPIO_Pin=GPIO_Pin_4|GPIO_Pin_5; g.GPIO_Mode=GPIO_Mode_Out_PP; g.GPIO_Speed=GPIO_Speed_Very_High; GPIO_Init(GPIOC,&g);
    GPIOF->BSHR=GPIO_Pin_6|GPIO_Pin_7; g.GPIO_Pin=GPIO_Pin_6|GPIO_Pin_7; GPIO_Init(GPIOF,&g);
    g.GPIO_Mode=GPIO_Mode_AF_PP;
    g.GPIO_Pin=GPIO_Pin_2; GPIO_PinAFConfig(GPIOB,GPIO_PinSource2,GPIO_AF9); GPIO_Init(GPIOB,&g);
    g.GPIO_Pin=GPIO_Pin_8|GPIO_Pin_9;
    GPIO_PinAFConfig(GPIOF,GPIO_PinSource8,GPIO_AF10); GPIO_PinAFConfig(GPIOF,GPIO_PinSource9,GPIO_AF10); GPIO_Init(GPIOF,&g);
    qspi_configure();
    Delay_Ms(2);

    board_memory.ram_probed=1;
    if(!xfer(1,0x66,0,0,0,0,0,0) && !xfer(1,0x99,0,0,0,0,0,0)) {
        Delay_Ms(1);
        memset(id2,0xff,sizeof(id2));
        if(!xfer(1,0x9f,24,0,0,board_memory.ram_id,8,1) &&
           !xfer(1,0x9f,24,0,0,id2,8,1) &&
           board_memory.ram_id[0]==0x0d && board_memory.ram_id[1]==0x5d &&
           !memcmp(board_memory.ram_id,id2,2))
            board_memory.ram_ok=1;
    }

    board_memory.nand_probed=1;
    memset(id2,0xff,sizeof(id2));
    if(!xfer(0,0xff,0,0,0,0,0,0)) Delay_Ms(1);
    if(!xfer(0,0x9f,0,0,8,board_memory.nand_id,2,1) &&
       !xfer(0,0x9f,0,0,8,id2,2,1) &&
       board_memory.nand_id[0]==0xc8 && board_memory.nand_id[1]==0x92 &&
       !memcmp(board_memory.nand_id,id2,2) && !ready(1000,&v)) {
        v=0x10; /* ECC on; no OTP, no irreversible locking, standard SPI */
        if(!feature(0xb0,&v,0) && !feature(0xb0,&v,1) && v==0x10) {
            v=0; /* volatile block protection only */
            if(!feature(0xa0,&v,0) && !feature(0xa0,&v,1) && v==0) board_memory.nand_ok=1;
        }
    }
    if(board_memory.ram_ok) {
        r=PSRAM_SelfTest();
        if(r) { board_memory.ram_ok=0; board_memory.last_error=r; }
    }

    /* Absence is not an error. Leave chip selects safe and shut down an
     * otherwise unused controller so boards without either footprint fitted
     * run exactly like the original internal-RAM-only design. */
    GPIOC->BSHR=GPIO_Pin_4|GPIO_Pin_5;
    if(!board_memory.ram_ok && !board_memory.nand_ok) {
        qspi_recover();
        QSPI_Cmd(QSPI1,DISABLE);
        RCC_HB1PeriphClockCmd(RCC_HB1Periph_QSPI1,DISABLE);
        board_memory.last_error=MEM_OK;
    }
}
static int ram_io(uint32_t a,void *p,uint32_t n,uint8_t read)
{
    uint8_t *b=p;
    if(!board_memory.ram_ok) return MEM_ID;
    if(a>PSRAM_BYTES || n>PSRAM_BYTES-a) return MEM_BOUNDS;
    while(n) {
        uint32_t k=n>8?8:n;
        int r=xfer(1,read?3:2,24,a,0,b,k,read); if(r) return r;
        a+=k; b+=k; n-=k;
    }
    return 0;
}
int PSRAM_Read(uint32_t a,void *p,uint32_t n) { return ram_io(a,p,n,1); }
int PSRAM_Write(uint32_t a,const void *p,uint32_t n) { return ram_io(a,(void*)p,n,0); }
int PSRAM_SelfTest(void)
{
    uint32_t a,v,r;
    /* All 23 address lines; boot only, PSRAM has no persistence contract. */
    for(a=4;a<PSRAM_BYTES;a<<=1) { v=0x96a50000u^a; if(PSRAM_Write(a,&v,4)) return MEM_TIMEOUT; }
    for(a=4;a<PSRAM_BYTES;a<<=1) { v=0x96a50000u^a; if(PSRAM_Read(a,&r,4)||r!=v) return MEM_BAD; }
    v=0xa55ac33c; if(PSRAM_Write(PSRAM_BYTES-4,&v,4)||PSRAM_Read(PSRAM_BYTES-4,&r,4)||r!=v) return MEM_BAD;
    return 0;
}
int NAND_Read(uint32_t page,uint16_t col,void *p,uint16_t n)
{
    uint8_t s; int r;
    if(!board_memory.nand_ok) return MEM_ID;
    if(page>=NAND_BLOCKS*64u || col>2112 || n>2112-col) return MEM_BOUNDS;
    if((r=xfer(0,0x13,24,page,0,0,0,0)) || (r=ready(100,&s))) return r;
    /* Bad block marker must remain readable even when a defective page fails ECC. */
    if(col<2048 && (s&0x30)==0x20) return MEM_ECC;
    if((s&0x30)!=0) ++board_memory.corrected;
    return xfer(0,3,16,col,8,p,n,1);
}
int NAND_BadBlock(uint16_t b)
{
    uint8_t marker; int r=NAND_Read((uint32_t)b*64,2048,&marker,1);
    return r ? r : marker!=0xff;
}
int NAND_Program(uint32_t page,const void *p)
{
    uint8_t s; int r;
    if(!board_memory.nand_ok) return MEM_ID;
    if(page>=NAND_BLOCKS*64u) return MEM_BOUNDS;
    if((r=wren()) || (r=xfer(0,2,16,0,0,(void*)p,2048,0)) ||
       (r=xfer(0,0x10,24,page,0,0,0,0)) || (r=ready(1000,&s))) return r;
    return s&8 ? MEM_PROGRAM : 0;
}
int NAND_Erase(uint16_t b)
{
    uint8_t s; int r=NAND_BadBlock(b);
    if(r) return r<0 ? r : MEM_BAD;
    if((r=wren()) || (r=xfer(0,0xd8,24,(uint32_t)b*64,0,0,0,0)) || (r=ready(2000,&s))) return r;
    return s&4 ? MEM_ERASE : 0;
}
#endif
