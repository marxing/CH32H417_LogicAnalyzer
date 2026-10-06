/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.c
* Author             : Q2H2
* Version            : V1.0.0
* Date               : 2026/04/29
* Description        : This file main functions.
********************************************************************************/
#include "hardware.h"
#include "ch32h417_usbss_device.h"
#include "ch32h417_usbhs_device.h"
#include "ch32h417_logic.h"
#include "board_ui.h"
#include "instrument.h"
#ifndef APP_BOOT_DIAGNOSTIC
#define APP_BOOT_DIAGNOSTIC 0
#endif
/* USB3 Type-C orientation control for U2 (FSW3820).
 * Rev 2026-09-12: PF10=EN_N, PD8=SEL, PA0=CC2, PA1=CC1.
 * Preserve previously tested lane polarity; PF6/PF7 now belong to memory.
 */
#define USB3_MUX_EN_PORT          GPIOF
#define USB3_MUX_EN_PIN           GPIO_Pin_10
#define USB3_MUX_SEL_PORT         GPIOD
#define USB3_MUX_SEL_PIN          GPIO_Pin_8
#define USB_CC2_SENSE_PORT        GPIOA
#define USB_CC2_SENSE_PIN         GPIO_Pin_0
#define USB_CC1_SENSE_PORT        GPIOA
#define USB_CC1_SENSE_PIN         GPIO_Pin_1
/* FSW3820 truth table: SEL=0 selects port A (Type-C lane set 1),
 * SEL=1 selects port B (Type-C lane set 2). */
#define USB3_MUX_ORIENT_CC1       0u
#define USB3_MUX_ORIENT_CC2       1u
static uint8_t usb3_mux_orientation = 0xffu;
static uint8_t cc_ready;
static uint8_t usb3_mux_mode = USB3_MUX_MODE_AUTO;
static uint8_t usb3_mux_enabled;
static uint8_t usb3_mux_sel;
static uint16_t board_cc_raw[2] = {0xffffu, 0xffffu};
uint16_t board_cc_mv[2];
static uint16_t cc_read_raw(uint8_t channel)
{
    uint32_t budget=5000;
    ADC_RegularChannelConfig(ADC1,channel,1,ADC_SampleTime_CyclesMode7);
    ADC_ClearFlag(ADC1,ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(ADC1,ENABLE);
    while(!ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC) && budget) --budget;
    return budget ? ADC_GetConversionValue(ADC1) : 0xffffu;
}

static uint16_t cc_raw_to_mv(uint16_t raw)
{
    return raw == 0xffffu ? 0xffffu : (uint32_t)raw * 3300u / 4095u;
}

static void usb3_mux_disable(void)
{
    GPIO_SetBits(USB3_MUX_EN_PORT, USB3_MUX_EN_PIN);
    usb3_mux_enabled = 0u;
    usb3_mux_orientation = 0xffu;
}

static void usb3_mux_apply(uint8_t orientation)
{
    if(usb3_mux_enabled != 0u && usb3_mux_orientation == orientation) return;
    GPIO_SetBits(USB3_MUX_EN_PORT, USB3_MUX_EN_PIN);
    if(orientation != 0u) GPIO_SetBits(USB3_MUX_SEL_PORT, USB3_MUX_SEL_PIN);
    else GPIO_ResetBits(USB3_MUX_SEL_PORT, USB3_MUX_SEL_PIN);
    usb3_mux_sel = orientation;
    Delay_Ms(1);
    GPIO_ResetBits(USB3_MUX_EN_PORT, USB3_MUX_EN_PIN);
    usb3_mux_enabled = 1u;
    usb3_mux_orientation = orientation;
}

uint8_t USB3_MUX_SetMode(uint8_t mode)
{
    if(mode > USB3_MUX_MODE_DISABLED) return 0u;
    usb3_mux_mode = mode;
    if(mode == USB3_MUX_MODE_PORT_A) usb3_mux_apply(0u);
    else if(mode == USB3_MUX_MODE_PORT_B) usb3_mux_apply(1u);
    else if(mode == USB3_MUX_MODE_DISABLED) usb3_mux_disable();
    return 1u;
}

void USB3_MUX_GetDiag(USB3_MUX_DIAG *diag)
{
    uint8_t cc1_active;
    uint8_t cc2_active;
    if(diag == 0) return;
    diag->cc1_raw = board_cc_raw[0];
    diag->cc2_raw = board_cc_raw[1];
    diag->cc1_mv = board_cc_mv[0];
    diag->cc2_mv = board_cc_mv[1];
    diag->mode = usb3_mux_mode;
    cc1_active = board_cc_mv[0] >= 200u && board_cc_mv[0] <= 2200u;
    cc2_active = board_cc_mv[1] >= 200u && board_cc_mv[1] <= 2200u;
    diag->state = (cc1_active ? 0x01u : 0u) |
                  (cc2_active ? 0x02u : 0u) |
                  (cc_ready ? 0x04u : 0u) | (usb3_mux_enabled ? 0x08u : 0u) |
                  (usb3_mux_sel ? 0x10u : 0u) |
                  (usb3_mux_orientation != 0xffu ? 0x20u : 0u) |
                  (usb3_mux_orientation == 1u ? 0x40u : 0u);
}

void USB3_MUX_Update(void)
{
    uint8_t cc1_active;
    uint8_t cc2_active;
    uint8_t orientation;
    static uint8_t candidate=0xff, stable_count;
    static uint32_t last;
    if(!cc_ready || Board_UI_Millis()-last<10) return;
    last=Board_UI_Millis();

    board_cc_raw[0]=cc_read_raw(ADC_Channel_1);
    board_cc_raw[1]=cc_read_raw(ADC_Channel_0);
    board_cc_mv[0]=cc_raw_to_mv(board_cc_raw[0]);
    board_cc_mv[1]=cc_raw_to_mv(board_cc_raw[1]);
    if(usb3_mux_mode == USB3_MUX_MODE_PORT_A) { usb3_mux_apply(0u); return; }
    if(usb3_mux_mode == USB3_MUX_MODE_PORT_B) { usb3_mux_apply(1u); return; }
    if(usb3_mux_mode == USB3_MUX_MODE_DISABLED) { usb3_mux_disable(); return; }

    cc1_active=board_cc_mv[0]>=200u && board_cc_mv[0]<=2200u;
    cc2_active=board_cc_mv[1]>=200u && board_cc_mv[1]<=2200u;
    orientation=cc1_active==cc2_active ? 0xffu :
                (cc1_active ? USB3_MUX_ORIENT_CC1 : USB3_MUX_ORIENT_CC2);
    if(orientation!=candidate) { candidate=orientation; stable_count=0u; }
    if(stable_count<3u) { ++stable_count; return; }
    /* An unplugged/noisy/temporarily invalid CC sample must not tear down a
     * working SuperSpeed path. Keep the last valid orientation and wait for
     * a later unambiguous, debounced sample. */
    if(cc1_active==cc2_active) return;
    orientation=cc1_active ? USB3_MUX_ORIENT_CC1 : USB3_MUX_ORIENT_CC2;
    if(orientation==usb3_mux_orientation) return;
    usb3_mux_apply(orientation);
}

void USB3_MUX_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_InitTypeDef adc={0};
    uint32_t budget;
    uint8_t i, cc1_votes=0u, cc2_votes=0u;
    uint8_t orientation=USB3_MUX_ORIENT_CC2;

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOF | RCC_HB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin = USB3_MUX_EN_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOF, &gpio);
    gpio.GPIO_Pin = USB3_MUX_SEL_PIN;
    GPIO_Init(GPIOD, &gpio);

    gpio.GPIO_Pin = USB_CC1_SENSE_PIN | USB_CC2_SENSE_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ADC1,ENABLE);
    RCC_ADCCLKConfig(RCC_ADCCLKSource_HCLK);
    /* Match the CH32H417 vendor ADC polling example: ADC clock is
     * HCLK / 2 / 8.  The previous HCLK / 16 / 8 setup left both software
     * conversions timing out on the target board. */
    RCC_ADCHCLKCLKAsSourceConfig(RCC_PPRE2_DIV2,RCC_HCLK_ADCPRE_DIV8);
    ADC_DeInit(ADC1); adc.ADC_Mode=ADC_Mode_Independent;
    adc.ADC_ScanConvMode=DISABLE;
    adc.ADC_ContinuousConvMode=DISABLE;
    adc.ADC_ExternalTrigConv=ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign=ADC_DataAlign_Right; adc.ADC_NbrOfChannel=1;
    adc.ADC_OutputBuffer=ADC_OutputBuffer_Disable;
    adc.ADC_Pga=ADC_Pga_1;
    ADC_Init(ADC1,&adc);
    ADC_LowPowerModeCmd(ADC1,ENABLE);
    ADC_Cmd(ADC1,ENABLE);
    ADC_ResetCalibration(ADC1); budget=100000;
    while(ADC_GetResetCalibrationStatus(ADC1) && budget) --budget;
    if(budget) {
        ADC_StartCalibration(ADC1); budget=100000;
        while(ADC_GetCalibrationStatus(ADC1) && budget) --budget;
        ADC_BufferCmd(ADC1,DISABLE);
        if(budget) {
            /* Calibration completion alone is insufficient. Prove that both
             * regular channels can actually finish a conversion before CC
             * values are allowed to steer the SuperSpeed MUX. */
            board_cc_raw[0]=cc_read_raw(ADC_Channel_1);
            board_cc_raw[1]=cc_read_raw(ADC_Channel_0);
            cc_ready=(board_cc_raw[0]!=0xffffu && board_cc_raw[1]!=0xffffu);
        }
    }

    /* Start disabled while CC is sampled. User-confirmed board mapping:
     * CC1 active -> Port A/SEL=0; CC2 active -> Port B/SEL=1. Require a clear
     * majority and retain the known-enumerating Port B fallback if CC is
     * absent or ambiguous. Selection is complete before USBSS link training. */
    GPIO_SetBits(USB3_MUX_EN_PORT, USB3_MUX_EN_PIN);
    GPIO_ResetBits(USB3_MUX_SEL_PORT, USB3_MUX_SEL_PIN);
    usb3_mux_enabled = 0u;
    usb3_mux_sel = 0u;
    usb3_mux_mode = USB3_MUX_MODE_AUTO;
    if(cc_ready) {
        for(i=0u;i<6u;++i) {
            uint8_t cc1_active, cc2_active;
            board_cc_raw[0]=cc_read_raw(ADC_Channel_1);
            board_cc_raw[1]=cc_read_raw(ADC_Channel_0);
            board_cc_mv[0]=cc_raw_to_mv(board_cc_raw[0]);
            board_cc_mv[1]=cc_raw_to_mv(board_cc_raw[1]);
            cc1_active=board_cc_mv[0]>=200u && board_cc_mv[0]<=2200u;
            cc2_active=board_cc_mv[1]>=200u && board_cc_mv[1]<=2200u;
            if(cc1_active && !cc2_active) ++cc1_votes;
            if(cc2_active && !cc1_active) ++cc2_votes;
            Delay_Ms(2);
        }
        if(cc1_votes>=4u && cc1_votes>cc2_votes) orientation=USB3_MUX_ORIENT_CC1;
        else if(cc2_votes>=4u && cc2_votes>cc1_votes) orientation=USB3_MUX_ORIENT_CC2;
    }
    usb3_mux_apply(orientation);
}
uint16_t DAC_Value[7] = {700, 600, 500, 0, 750, 750, 750};    // Voltage must be less than 0.6V before it can be increased. When it is 0, it is 3.3V

/*********************************************************************
 * @fn      VDDI8_IO_Change
 *
 * @brief   Turn off the internal 1.8v power supply .
 *
 * @param   none
 *
 * @return  none
 */
void VDDI8_IO_Change(void)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR->CTLR |= PWR_CTLR_VIO_SWCR;
    PWR->CTLR &= ~PWR_CTLR_VSEL_VIO18;
    PWR->CTLR = PWR->CTLR | PWR_CTLR_VSEL_VIO18_MODE5;
    Dac_Init(); /* Default output 3.3V power supply */
}

/*********************************************************************
 * @fn      Hardware
 *
 * @brief   Logic&Adc main process
 *
 * @param   none
 *
 * @return  none
 */
void Hardware(void)
{
    /* Disable SWD */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    /* Enable the IO function of PC6 pins */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI->OR |= SWPMI_SWP_TBYP;
    /* Parameter initialization */
    LOGIC_Para_Init();
    ADC_AFE_Init();
    Led_Time_Init();
    Delay_Ms(100);
    VDDI8_IO_Change();                                                          /* Turn off VDDIO-1.8V power supply */
    Get_Flash_Size();                                                           /* Get FLASH size for IAP upgrade */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_HSADC, ENABLE);   /* Enable ADC clock */
    RCC_HSADCCLKConfig(RCC_HSADCSource_PLLCLK);                                 /* Enable ADC PLL */
    /* Establish the USB transport before probing optional local peripherals.
     * Control transfers are interrupt driven; the command loop starts after
     * bounded UI/memory initialization below. */
    USB3_MUX_Init();
    USB_Timer_Init();
    USBSS_Device_Init(ENABLE);
#if !APP_BOOT_DIAGNOSTIC || APP_BOOT_DIAGNOSTIC == 3
    Board_UI_Init();
    Instrument_Init();
#elif APP_BOOT_DIAGNOSTIC == 2
    DBG_PRINT("BOOT DIAG: LCD and keys only, offline and external memory disabled\r\n");
    Board_UI_Init();
    DBG_PRINT("BOOT DIAG: LCD initialization completed\r\n");
#else
    /* Isolation build: never touch LCD/QSPI/offline acquisition on boot. */
    DBG_PRINT("BOOT DIAG: USB baseline, LCD and external memory disabled\r\n");
#endif
    DBG_PRINT("BOOT: USB initialized, main loop entered\r\n");
    while(1)
    {
        USB3_MUX_Update();
        LOGIC_ADC_CMD_Process();                                                /* Logic analyzer and ADC acquisition parameter setting main function */
        LOGIC_UHSIF_Main_Process();                                             /* Logic analyzer data transfer main function */
        LOGIC_ADC_Main_Process();                                               /* ADC data acquisition main function */
        LOGIC_ADC_RAMOVER_Process();                                            /* Report buffer overflow main function */
        IAP_JUMP_Process();                                                     /* IAP processing main function */
#if !APP_BOOT_DIAGNOSTIC || APP_BOOT_DIAGNOSTIC == 3
        Instrument_Process();
        Board_UI_Process();
#elif APP_BOOT_DIAGNOSTIC == 2
        Board_UI_Process();
#endif
    }
}
