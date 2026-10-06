/********************************** (C) COPYRIGHT  *******************************
 * File Name          : ch32h417_logic.c
 * Author             : Q2H2
 * Version            : V1.0.0
 * Date               : 2026/05/18
 * Description        : Core logic analyzer firmware functions for CH32H417, 
 *                      including clock configuration and HSADC handling.
 ********************************************************************************/
#include <string.h>
#include "ch32h417_logic.h"
#include "ch32h417_it.h"
#include "ch32h417_uhsif.h"
#include "ch32h417_usb.h"
#include "ch32h417_usbss_device.h"
#include "usb_desc.h"
#include "board_ui.h"
#include "instrument.h"
#include "mw_command_guard.h"
#include "hardware.h"

uint8_t adc_flag = 0;
volatile uint16_t test_flag = 0;
volatile uint32_t Flash_Erase_Page_Size = Size_8KB;
LOGIC_ADC_PARA logic_adc_info;
__attribute__((aligned(32), section(".da1"))) uint16_t ADC_BufA[ADC_BUF_LEN + 1024]; // HSADC DMA buffer A in shared SRAM
__attribute__((aligned(32), section(".da1"))) uint16_t ADC_BufB[ADC_BUF_LEN + 1024]; // HSADC DMA buffer B in shared SRAM

void HSADC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/* EP1 RX and TX share one command transaction. Keep RX NAKed until the
 * response has actually completed, and recover the command pipe if a host
 * abandons an IN transfer. */
static volatile uint8_t command_tx_busy;
static volatile uint8_t command_rx_held;
static uint32_t command_tx_started;
static uint32_t adc_ep3_tx_started;

#define COMMAND_TX_TIMEOUT_MS 1000u
#define ADC_EP3_TX_TIMEOUT_MS 100u

static void Command_Release_RX(void);

void Command_Transport_Reset(void)
{
    command_tx_busy = 0u;
    command_rx_held = 0u;
    logic_adc_info.usb20_endp1_down = 0u;
    logic_adc_info.usb30_endp1_down = 0u;
}

void Command_Tx_Complete(void)
{
    command_tx_busy = 0u;
    /* The command OUT endpoint must become ready as part of the completed
     * transaction. Deferring this to the foreground loop can strand EP1 in
     * NAK when that loop is delayed by a capture or recovery path. */
    Command_Release_RX();
}

static void Command_Release_RX(void)
{
    if(command_rx_held == 0u) return;
    command_rx_held = 0u;
    __asm volatile("fence" ::: "memory");
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
    {
        USBSSD->EP1_RX.UEP_RX_DMA = (uint32_t)USBSS_EP1_Rx_Buf;
        USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP = 1u;
    }
    else if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {
        USBHSD->UEP1_RX_CTRL = (USBHSD->UEP1_RX_CTRL & ~USBHS_UEP_R_RES_MASK) |
                               USBHS_UEP_R_RES_ACK;
    }
}

static void Command_Transport_Arm(void)
{
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
    {
        /* Drop a response abandoned by the host before accepting another
         * command. Clearing only the software flags leaves the old IN chain
         * resident in the USBSS endpoint. */
        USBSSD->EP1_TX.UEP_TX_CR = USBSS_EP_TX_CLR | USBSS_EP_TX_CHAIN_CLR;
        USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = 0u;
        USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = 0u;
        USBSSD->EP1_TX.UEP_TX_CHAIN_ST |= USBSS_EP_TX_CHAIN_IF;
        __asm volatile("fence" ::: "memory");
        Command_Transport_Reset();
        USBSSD->EP1_RX.UEP_RX_DMA = (uint32_t)USBSS_EP1_Rx_Buf;
        USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP = 1u;
    }
    else if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {
        USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) |
                               USBHS_UEP_T_RES_NAK;
        USBHSD->UEP1_TX_LEN = 0u;
        USBHSD->UEP1_TX_CTRL &= ~USBHS_UEP_T_DONE;
        __asm volatile("fence" ::: "memory");
        Command_Transport_Reset();
        USBHSD->UEP1_RX_CTRL = (USBHSD->UEP1_RX_CTRL & ~USBHS_UEP_R_RES_MASK) |
                               USBHS_UEP_R_RES_ACK;
    }
    else
    {
        Command_Transport_Reset();
    }
}

static void Command_Submit(void)
{
    command_tx_started = Board_UI_Millis();
    command_tx_busy = 1u;
    __asm volatile("fence" ::: "memory");
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
    {
        USBSSD->EP1_TX.UEP_TX_DMA = (uint32_t)USBSS_EP1_Tx_Buf;
        USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = 32u;
        USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = 1u;
    }
    else if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {
        USBHSD->UEP1_TX_DMA = (uint32_t)USBSS_EP1_Tx_Buf;
        USBHSD->UEP1_TX_LEN = 32u;
        USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) |
                               USBHS_UEP_T_RES_ACK;
    }
    else
    {
        command_tx_busy = 0u;
    }
}

static uint8_t Command_Service(void)
{
    if(command_tx_busy != 0u)
    {
        if((uint32_t)(Board_UI_Millis() - command_tx_started) >= COMMAND_TX_TIMEOUT_MS)
        {
            Command_Transport_Arm();
        }
        return 1u;
    }
    Command_Release_RX();
    return 0u;
}

/*********************************************************************
 * @fn      UHSIF_Clock_Set
 *
 * @brief   Initializes Logic sys PLL & sys CLK.
 * @param   rcc_pll  See Bit definition for RCC_PLLCFGR register
 * @return  none
 */
void UHSIF_Clock_Set(uint32_t rcc_pll)
{
    /* Repeated captures normally use the same PLL. Switching it off while
     * USB is running introduces an unnecessary clock interruption. */
    if((RCC->CFGR0 & RCC_SWS) == RCC_SWS_PLL &&
       (RCC->CTLR & RCC_PLLRDY) &&
       (RCC->PLLCFGR & (RCC_PLLMUL | RCC_PLL_SRC_DIV | RCC_PLLSRC)) ==
       (rcc_pll | RCC_PLL_SRC_DIV1 | RCC_PLLSRC_HSE))
        return;
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_HSI;
    while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_HSI);    // Wait for clock to stabilize

    RCC->PLLCFGR &= ~0x80000000;
    RCC->CTLR &= ~RCC_PLLON;

    RCC->PLLCFGR &= (uint32_t)((uint32_t)~(RCC_PLLMUL));
    RCC->PLLCFGR |= (uint32_t)rcc_pll;
    RCC->PLLCFGR &= (uint32_t)((uint32_t)~(RCC_PLL_SRC_DIV));
    RCC->PLLCFGR |= (uint32_t)RCC_PLL_SRC_DIV1;
    RCC->PLLCFGR &= (uint32_t)((uint32_t)~(RCC_PLLSRC));
    RCC->PLLCFGR |= (uint32_t)RCC_PLLSRC_HSE;

    RCC->CTLR |= RCC_PLLON;
    while((RCC->CTLR & RCC_PLLRDY) == 0);
    RCC->PLLCFGR |= 0x80000000;

    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;    // Switch to PLL clock, wait for clock to stabilize
    while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);
    SystemAndCoreClockUpdate();
    Delay_Init();
    Led_Time_Init(); /* PLL changes must not change the 5-ms key timebase. */
    USART_Printf_Init(921600);    // Reinit baud rate
    Delay_Ms(100);                // Need delay time
}

/*********************************************************************
 * @fn      Set_Logic_Para_Fun
 *
 * @brief   Initializes Logic sys collection.
 *
 * @return  none
 */
void Set_Logic_Para_Fun(uint8_t logic_bit, uint8_t logic_sys)
{
    uint8_t uhsif_div;
    uint32_t rcc_pll;
    switch(logic_sys)
    {
    case 0x00:    // 200M  400/2
        rcc_pll = RCC_PLLMUL16;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x01:    // 187M 25*15/2 =187.5
        rcc_pll = RCC_PLLMUL15;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x02:    // 175  25*14/2 = 175
        rcc_pll = RCC_PLLMUL14;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x03:    // 162M 25*13/2 = 162.5
        rcc_pll = RCC_PLLMUL13;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x04:    // 156M  25*12.5/2 = 156.25
        rcc_pll = RCC_PLLMUL12_5;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x05:    // 25*12/2 = 150
        rcc_pll = RCC_PLLMUL12;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x06:    // 25*11/2 = 137.5
        rcc_pll = RCC_PLLMUL11;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x07:    // 25*10/2 = 125
        rcc_pll = RCC_PLLMUL10;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x08:    // 25*9/2 = 112.5
        rcc_pll = RCC_PLLMUL9;
        uhsif_div = RCC_UHSIFDIV_DIV2;
        break;
    case 0x09:    // 100
        rcc_pll = RCC_PLLMUL16;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0a:    // 187.5 /2 =
        rcc_pll = RCC_PLLMUL15;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0b:    // 25*14/2 = 175/2 = 87.5
        rcc_pll = RCC_PLLMUL14;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0c:    // 162.5/2 = 81.5
        rcc_pll = RCC_PLLMUL13;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0d:    // 156.25/2 = 78.125
        rcc_pll = RCC_PLLMUL12_5;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0e:    // 150/2 = 75
        rcc_pll = RCC_PLLMUL12;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    case 0x0f:    // 137.5/2 = 68.75
        rcc_pll = RCC_PLLMUL11;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    default:    // Default 100M
        rcc_pll = RCC_PLLMUL16;
        uhsif_div = RCC_UHSIFDIV_DIV4;
        break;
    }
    logic_adc_info.uhsif_div = uhsif_div;
    if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {    // USB2.0 high speed, reconfigure buffer descriptor
        if(logic_bit == 8)
        {
            rcc_pll = RCC_PLLMUL16;                 // Default 400M
            uhsif_div = RCC_UHSIFDIV_DIV10;         // 10x divider, speed 40MB
        }
        else if(logic_bit == 16)
        {
            rcc_pll = RCC_PLLMUL16;                 // Default 400M
            uhsif_div = RCC_UHSIFDIV_DIV20;         // 20x divider, speed 20MB
        }
        logic_adc_info.uhsif_div = uhsif_div;
        UHSIF_Clock_Set(rcc_pll);                   // Configure system PLL clock
        UHSIF_Set_Para(logic_bit, uhsif_div);       // Configure parallel acquisition parameters
    }
    else
    {                                               // USB3.0 super speed
        UHSIF_Clock_Set(rcc_pll);                   // Configure system PLL clock
        UHSIF_Set_Para(logic_bit, uhsif_div);       // Configure parallel acquisition parameters
    }
}

/*********************************************************************
 * @fn      Dac_Init
 *
 * @brief   Initializes dac collection.
 *
 * @return  none
 */
void Dac_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };
    DAC_InitTypeDef DAC_InitType = { 0 };

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_DAC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    DAC_InitType.DAC_Trigger = DAC_Trigger_None;
    DAC_InitType.DAC_WaveGeneration = DAC_WaveGeneration_None;
    DAC_InitType.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_InitType.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &DAC_InitType);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_SetChannel1Data(DAC_Align_12b_R, 34);    // Default output 0, voltage 3.3V
}

/*********************************************************************
 * @fn      DAC_OUT
 *
 * @brief    dac out data.
 *
 * @return  none
 */
void DAC_OUT(uint16_t dac_value)
{
    DAC_SetChannel1Data(DAC_Align_12b_R, dac_value);
}

/*********************************************************************
 * @fn      ADC_AFE_Init
 *
 * @brief   Initializes the two TPG4735 range selectors in the widest,
 *          lowest-gain range selected by the schematic pull-downs.
 */
void ADC_AFE_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_GPIOE, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
    GPIO_InitStructure.GPIO_Pin = BOARD_AFE_CH1_S0_PIN;
    GPIO_Init(BOARD_AFE_CH1_S0_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = BOARD_AFE_CH1_S1_PIN |
                                 BOARD_AFE_CH2_S0_PIN |
                                 BOARD_AFE_CH2_S1_PIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_ResetBits(BOARD_AFE_CH1_S0_PORT, BOARD_AFE_CH1_S0_PIN);
    GPIO_ResetBits(GPIOC, BOARD_AFE_CH1_S1_PIN |
                          BOARD_AFE_CH2_S0_PIN |
                          BOARD_AFE_CH2_S1_PIN);
    logic_adc_info.adc_range[ADC_CHANNEL0] = ADC_RANGE_WIDE;
    logic_adc_info.adc_range[ADC_CHANNEL1] = ADC_RANGE_WIDE;
}

/*********************************************************************
 * @fn      ADC_AFE_SetRange
 *
 * @brief   Selects one of the four feedback resistor pairs.
 *
 * @return  1 on success, 0 for invalid parameters or active sampling.
 */
uint8_t ADC_AFE_SetRange(uint8_t channel, uint8_t range)
{
    GPIO_TypeDef *s0_port;
    GPIO_TypeDef *s1_port;
    uint16_t s0_pin;
    uint16_t s1_pin;

    if((channel > ADC_CHANNEL1) || (range >= ADC_RANGE_COUNT) ||
       (logic_adc_info.adc_active != 0u))
    {
        return 0u;
    }

    if(channel == ADC_CHANNEL0)
    {
        s0_port = BOARD_AFE_CH1_S0_PORT;
        s0_pin = BOARD_AFE_CH1_S0_PIN;
        s1_port = BOARD_AFE_CH1_S1_PORT;
        s1_pin = BOARD_AFE_CH1_S1_PIN;
    }
    else
    {
        s0_port = BOARD_AFE_CH2_S0_PORT;
        s0_pin = BOARD_AFE_CH2_S0_PIN;
        s1_port = BOARD_AFE_CH2_S1_PORT;
        s1_pin = BOARD_AFE_CH2_S1_PIN;
    }

    GPIO_WriteBit(s0_port, s0_pin, (range & 0x01u) ? Bit_SET : Bit_RESET);
    GPIO_WriteBit(s1_port, s1_pin, (range & 0x02u) ? Bit_SET : Bit_RESET);
    logic_adc_info.adc_range[channel] = range;
    Delay_Us(ADC_AFE_SETTLE_US);
    return 1u;
}

uint8_t ADC_AFE_GetRange(uint8_t channel)
{
    if(channel > ADC_CHANNEL1)
    {
        return ADC_RANGE_WIDE;
    }
    return logic_adc_info.adc_range[channel];
}

/*********************************************************************
 * @fn      HSADC_Function_Init
 *
 * @brief   Initializes HSADC collection.
 * @return  none
 */
void HSADC_Function_Init(void)
{
    HSADC_InitTypeDef HSADC_InitStructure = { 0 };
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_HSADC, ENABLE);
    RCC_HSADCCLKConfig(RCC_HSADCSource_PLLCLK);

    /* Both AFE outputs are analog pins on the new schematic. */
    GPIO_InitStructure.GPIO_Pin = BOARD_ADC_CH1_GPIO_PIN | BOARD_ADC_CH2_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(BOARD_ADC_GPIO_PORT, &GPIO_InitStructure);

    /* Only two physical ADC inputs exist on this board. */
    if(logic_adc_info.adc_channel != ADC_CHANNEL1)
    {
        logic_adc_info.adc_channel = ADC_CHANNEL0;
    }
    if(logic_adc_info.adc_div < ADC_HSADC_MIN_DIV)
    {
        logic_adc_info.adc_div = ADC_HSADC_MIN_DIV;
    }
    else if(logic_adc_info.adc_div > ADC_HSADC_MAX_DIV)
    {
        logic_adc_info.adc_div = ADC_HSADC_MAX_DIV;
    }
    HSADC_InitStructure.HSADC_BurstMode = ENABLE;
    HSADC_InitStructure.HSADC_DMA_TransferLen = ADC_BUF_LEN - 1;                // 1024*16-1; // 16K per acquisition
    HSADC_InitStructure.HSADC_BurstMode_TransferLen = 3;
    HSADC_InitStructure.HSADC_BurstMode_DMA_LastTransferLen = 16;

    HSADC_InitStructure.HSADC_ClockDivision = logic_adc_info.adc_div;           // ADC divider: 400/(adc_div+1)/5 = adc sys
    HSADC_InitStructure.HSADC_DMA = ENABLE;
    HSADC_InitStructure.HSADC_RxAddress0 = (uint32_t)ADC_BufA;
    HSADC_InitStructure.HSADC_RxAddress1 = (uint32_t)ADC_BufB;

    HSADC_InitStructure.HSADC_DualBuffer = ENABLE;
    HSADC_InitStructure.HSADC_FirstConversionCycle = HSADC_First_Conversion_Cycle_8;
    if(logic_adc_info.adc_bit == ADC_GET_WIDTH8)
    {
        HSADC_InitStructure.HSADC_DataSize = HSADC_DataSize_8b;
    }
    else
    {
        logic_adc_info.adc_bit = ADC_GET_WIDTH10;
        HSADC_InitStructure.HSADC_DataSize = HSADC_DataSize_16b;
    }
    HSADC_Init(&HSADC_InitStructure);
    /* HSADC_BURSTEND is a command that terminates the active burst.  It must
     * stay clear for continuous ping-pong DMA streaming. */
    HSADC_BurstEndCmd(DISABLE);
    HSADC_ChannelConfig((logic_adc_info.adc_channel == ADC_CHANNEL1) ?
                        BOARD_ADC_CH2_HSADC_CHANNEL :
                        BOARD_ADC_CH1_HSADC_CHANNEL);
    HSADC->STATR = 0x07;
    NVIC_ClearPendingIRQ(HSADC_IRQn);
    NVIC_SetPriority(HSADC_IRQn, 2);
    HSADC_ITConfig(HSADC_IT_DMAEnd, ENABLE);
    logic_adc_info.usb30_endp3_up = USB_SUCESEE_FLAG;
    logic_adc_info.usb20_endp3_up = USB_SUCESEE_FLAG;
    logic_adc_info.adc_rec_total = 0;
    logic_adc_info.adc_flag = 0;
    logic_adc_info.adc_ready_mask = 0;
    logic_adc_info.adc_overrun_count = 0;
    logic_adc_info.adc_dma_count = 0;
    logic_adc_info.adc_usb_count = 0;
    logic_adc_info.usb_endp3_up_count = 0;
    adc_ep3_tx_started = 0u;
    __asm volatile("fence" ::: "memory");
    NVIC_EnableIRQ(HSADC_IRQn);
}

/*********************************************************************
 * @fn      HSADC_Function_Start
 *
 * @brief   Starts HSADC acquisition, reconfiguring system PLL and enabling conversion.
 *
 * @return  none
 */
void HSADC_Function_Start(void)
{
    if(logic_adc_info.adc_active != 0u)
    {
        HSADC_Function_Stop();
    }
    UHSIF_Clock_Set(RCC_PLLMUL16);    // Configure system PLL clock, 400M system clock 26-05-09
    (void)ADC_AFE_SetRange(logic_adc_info.adc_channel,
                           logic_adc_info.adc_range[logic_adc_info.adc_channel]);
    HSADC_Function_Init();
    HSADC_Cmd(ENABLE);
    logic_adc_info.adc_active = 1u;
    HSADC_SoftwareStartConvCmd(ENABLE);
}

/*********************************************************************
 * @fn      HSADC_Function_Stop
 *
 * @brief   Stops HSADC acquisition, disables interrupts, and resets the HSADC peripheral.
 *
 * @return  none
 */
void HSADC_Function_Stop(void)
{
	/* STOP is intentionally idempotent. In particular, logic-mode setup sends
	 * no ADC traffic and must not reset an already idle HSADC repeatedly. */
	if(logic_adc_info.adc_active == 0u)
	{
		logic_adc_info.adc_rec_total = 0u;
		logic_adc_info.adc_flag = 0u;
		logic_adc_info.adc_ready_mask = 0u;
		logic_adc_info.usb30_endp3_up = USB_SUCESEE_FLAG;
		logic_adc_info.usb20_endp3_up = USB_SUCESEE_FLAG;
		return;
	}
	logic_adc_info.adc_active = 0u;
    NVIC_DisableIRQ(HSADC_IRQn);
    HSADC_ITConfig(HSADC_IT_DMAEnd, DISABLE);
    HSADC_SoftwareStartConvCmd(DISABLE);
    HSADC_DMACmd(DISABLE);
    HSADC_Cmd(DISABLE);
    /* Retire the previous EP3 transfer before marking its software slot free.
     * Clear only the transfer chain: keep the USB packet sequence unchanged. */
    if(logic_adc_info.usb_status == USB_U3_CONNECT) {
        NVIC_DisableIRQ(USBSS_IRQn);
        USBSSD->EP3_TX.UEP_TX_CR |= USBSS_EP_TX_CHAIN_CLR;
        USBSSD->EP3_TX.UEP_TX_CHAIN_LEN = 0;
        USBSSD->EP3_TX.UEP_TX_CHAIN_ST |= USBSS_EP_TX_CHAIN_IF;
        NVIC_EnableIRQ(USBSS_IRQn);
    } else if(logic_adc_info.usb_status == USB_U2_CONNECT) {
        NVIC_DisableIRQ(USBHS_IRQn);
        USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP3_TX_LEN = 0;
        /* A completion can become pending while EP3 is being retired. If it
         * survives into the next ADC session, the old interrupt marks the new
         * transfer idle and lets the foreground overwrite its DMA address. */
        USBHSD->UEP3_TX_CTRL &= ~USBHS_UEP_T_DONE;
        __asm volatile("fence" ::: "memory");
        NVIC_EnableIRQ(USBHS_IRQn);
    }
    logic_adc_info.usb30_endp3_up = USB_SUCESEE_FLAG;
    logic_adc_info.usb20_endp3_up = USB_SUCESEE_FLAG;
    logic_adc_info.adc_rec_total = 0;
    logic_adc_info.adc_flag = 0;
    logic_adc_info.adc_ready_mask = 0;
    adc_ep3_tx_started = 0u;
    HSADC->STATR = 0x07;
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_HSADC, ENABLE);
    Delay_Us(2);
    RCC_HB2PeriphResetCmd(RCC_HB2Periph_HSADC, DISABLE);
    NVIC_ClearPendingIRQ(HSADC_IRQn);
    __asm volatile("fence" ::: "memory");
}

/*********************************************************************
 * @fn      LOGIC_Para_Init
 *
 * @brief   Initializes logic analyzer parameters to default values.
 *
 * @return  none
 */
void LOGIC_Para_Init(void)
{
    logic_adc_info.usb_status = USB_NO_CONNECT;
    logic_adc_info.usb30_endp1_down = USB_SUCESEE_FLAG;
    logic_adc_info.usb20_endp1_down = USB_SUCESEE_FLAG;
    logic_adc_info.logic_bit = 0;
    logic_adc_info.logic_sys = 0;
    logic_adc_info.uhsif_div = 0;
    logic_adc_info.logic_level = 0;
    logic_adc_info.adc_bit = ADC_GET_WIDTH10;
    logic_adc_info.adc_div = ADC_HSADC_MIN_DIV;
    logic_adc_info.adc_channel = ADC_CHANNEL0;
    logic_adc_info.adc_flag = 0;
    logic_adc_info.adc_range[ADC_CHANNEL0] = ADC_RANGE_WIDE;
    logic_adc_info.adc_range[ADC_CHANNEL1] = ADC_RANGE_WIDE;
    logic_adc_info.adc_ready_mask = 0;
    logic_adc_info.adc_active = 0;
    logic_adc_info.adc_overrun_count = 0;
    logic_adc_info.usb_ram_over_flag = 0;
    logic_adc_info.usb_trans_count = 0;
    logic_adc_info.usb_trans_flag = 0;
    Command_Transport_Reset();
    USB_RUN_ON( );
    USB_LINK_OFF( );
}

/*********************************************************************
 * @fn      LOGIC_ADC_CMD_Process
 *
 * @brief   Initializes Logic&adc collection.
 *
 * @return  none
 */
static void LOGIC_ADC_CMD_ProcessPending(void)
{
    uint8_t len = 0;
    uint8_t status;
    if((logic_adc_info.usb20_endp1_down == 0x00) && (logic_adc_info.usb30_endp1_down == 0x00))
    {
        return;
    }
    logic_adc_info.usb20_endp1_down = logic_adc_info.usb30_endp1_down = 0x00;    // Clear flag
    memset(USBSS_EP1_Tx_Buf,0,32);
    if(!mw_command_length_valid(pLogic_Para->Logic_Cmd, pLogic_Para->Logic_Len)) {
        if(pLogic_Para->Logic_Cmd >= 0xc0 && pLogic_Para->Logic_Cmd <= 0xc2) {
            USBSS_EP1_Tx_Buf[0]=pLogic_Para->Logic_Cmd;
            USBSS_EP1_Tx_Buf[1]=1; USBSS_EP1_Tx_Buf[2]=2;
            len=32; goto send_response;
        }
        LOGIC_ADC_CMD_Send(pLogic_Para->Logic_Cmd, 1);
        return;
    }
    if(Instrument_Command(pLogic_Para->Logic_Cmd,pLogic_Para->Logic_Buf,USBSS_EP1_Tx_Buf)) {
        len=32; goto send_response;
    }
    /* Queries stay available, mutations cannot change clocks/DMA during an
     * offline capture or NAND operation. Legacy streaming and local mode share
     * one owner, not two independently running producers. */
    if(Instrument_Busy() && pLogic_Para->Logic_Cmd!=CMD_SET_IAP &&
       pLogic_Para->Logic_Cmd!=CMD_GET_VERSION &&
       pLogic_Para->Logic_Cmd!=CMD_GET_USB3_MUX &&
       pLogic_Para->Logic_Cmd!=CMD_GET_ADC_RANGE && pLogic_Para->Logic_Cmd!=CMD_GET_ADC_PARA &&
       pLogic_Para->Logic_Cmd!=CMD_GET_ADC_CHANNEL && pLogic_Para->Logic_Cmd!=CMD_GET_LOGIC_PARA &&
       pLogic_Para->Logic_Cmd!=CMD_GET_LOGIC_LEVEL) {
        LOGIC_ADC_CMD_Send(pLogic_Para->Logic_Cmd,0x02); return;
    }
    if(logic_adc_info.usb_trans_flag && pLogic_Para->Logic_Cmd!=CMD_SET_STOP &&
       pLogic_Para->Logic_Cmd!=CMD_SET_ADC_STOP &&
       (pLogic_Para->Logic_Cmd==CMD_SET_LOGIC_PARA || pLogic_Para->Logic_Cmd==CMD_SET_LOGIC_LEVEL ||
        pLogic_Para->Logic_Cmd==CMD_SET_ADC_PARA || pLogic_Para->Logic_Cmd==CMD_SET_ADC_CHANNEL ||
        pLogic_Para->Logic_Cmd==CMD_SET_ADC_RANGE || pLogic_Para->Logic_Cmd==CMD_GET_ADC_SAMPLE ||
        pLogic_Para->Logic_Cmd==CMD_SET_USB3_MUX ||
        pLogic_Para->Logic_Cmd==CMD_SET_START || pLogic_Para->Logic_Cmd==CMD_SET_ADC_START)) {
        LOGIC_ADC_CMD_Send(pLogic_Para->Logic_Cmd,0x02); return;
    }
    switch(pLogic_Para->Logic_Cmd)
    {
        /* Logic commands */
    case CMD_SET_LOGIC_PARA: /* Set acquisition parameters */
        if((pLogic_Para->Logic_Buf[0]!=8 && pLogic_Para->Logic_Buf[0]!=16) || pLogic_Para->Logic_Buf[1]>15) {
            LOGIC_ADC_CMD_Send(CMD_SET_LOGIC_PARA,1); return;
        }
        logic_adc_info.logic_bit = pLogic_Para->Logic_Buf[0];
        logic_adc_info.logic_sys = pLogic_Para->Logic_Buf[1];
        Set_Logic_Para_Fun(logic_adc_info.logic_bit, logic_adc_info.logic_sys);
        pLogicSend_Para->Logic_Cmd = CMD_SET_LOGIC_PARA | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_SET_LOGIC_LEVEL: /* Set acquisition level */
        if(pLogic_Para->Logic_Buf[0]>15) { LOGIC_ADC_CMD_Send(CMD_SET_LOGIC_LEVEL,1); return; }
        logic_adc_info.logic_level = ((uint16_t)pLogic_Para->Logic_Buf[0] << 8) | pLogic_Para->Logic_Buf[1];
        DAC_OUT(logic_adc_info.logic_level);                                    // Set DAC output voltage, configure DAC first
        pLogicSend_Para->Logic_Cmd = CMD_SET_LOGIC_LEVEL | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_SET_START: /* Start acquisition */
        UHSIF_Set_Para(logic_adc_info.logic_bit, logic_adc_info.uhsif_div);     // Configure parallel acquisition parameters
        logic_adc_info.usb_ram_over_flag = 0;
        logic_adc_info.usb_trans_flag = ENABLE;
        logic_adc_info.usb_trans_count = 0;
        UHSIF_Start(ENABLE);                     // Start data acquisition
        pLogicSend_Para->Logic_Cmd = CMD_SET_START | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_SET_STOP: /* Stop acquisition */
        UHSIF_Start(DISABLE);                                                   // Stop data acquisition
        UHSIF_Para_Init();
        logic_adc_info.usb_trans_flag = DISABLE;
        logic_adc_info.usb_trans_count = 0;
        USB_LINK_ON();
        logic_adc_info.usb_ram_over_flag = 0;
        pLogicSend_Para->Logic_Cmd = CMD_SET_STOP | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_GET_LOGIC_PARA: /* Get acquisition parameters */
        pLogicSend_Para->Logic_Cmd = CMD_GET_LOGIC_PARA | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 3;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = logic_adc_info.logic_bit;
        pLogicSend_Para->Logic_Buf[1] = logic_adc_info.logic_sys;
        len = 5;
        break;
    case CMD_GET_LOGIC_LEVEL: /* Get acquisition level */
        pLogicSend_Para->Logic_Cmd = CMD_GET_LOGIC_LEVEL | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 3;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = (uint8_t)(logic_adc_info.logic_level >> 8);
        pLogicSend_Para->Logic_Buf[1] = (uint8_t)(logic_adc_info.logic_level);
        len = 5;
        break;

        /* ADC commands */
    case CMD_SET_ADC_PARA: /* Set ADC acquisition parameters */
        status = 0x00;
        if((pLogic_Para->Logic_Buf[0] != ADC_GET_WIDTH8) &&
           (pLogic_Para->Logic_Buf[0] != ADC_GET_WIDTH10))
        {
            status = 0x01;
        }
        if((pLogic_Para->Logic_Buf[1] < ADC_HSADC_MIN_DIV) ||
           (pLogic_Para->Logic_Buf[1] > ADC_HSADC_MAX_DIV))
        {
            status = 0x01;
        }
        if(status==0)
        {
            logic_adc_info.adc_bit = pLogic_Para->Logic_Buf[0];
            logic_adc_info.adc_div = pLogic_Para->Logic_Buf[1];
        }
        pLogicSend_Para->Logic_Cmd = CMD_SET_ADC_PARA | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = status;
        len = 3;
        break;
    case CMD_SET_ADC_CHANNEL: /* Set ADC acquisition channel */
        status = (pLogic_Para->Logic_Buf[0] <= ADC_CHANNEL1) ? 0x00 : 0x01;
        if(status == 0x00)
        {
            logic_adc_info.adc_channel = pLogic_Para->Logic_Buf[0];
        }
        pLogicSend_Para->Logic_Cmd = CMD_SET_ADC_CHANNEL | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = status;
        len = 3;
        break;
    case CMD_SET_ADC_RANGE:
        status = ADC_AFE_SetRange(pLogic_Para->Logic_Buf[0],
                                  pLogic_Para->Logic_Buf[1]) ? 0x00 : 0x01;
        pLogicSend_Para->Logic_Cmd = CMD_SET_ADC_RANGE | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = status;
        len = 3;
        break;
    case CMD_GET_ADC_RANGE:
        pLogicSend_Para->Logic_Cmd = CMD_GET_ADC_RANGE | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 3;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = ADC_AFE_GetRange(ADC_CHANNEL0);
        pLogicSend_Para->Logic_Buf[1] = ADC_AFE_GetRange(ADC_CHANNEL1);
        len = 5;
        break;
    case CMD_GET_ADC_SAMPLE: /* finite snapshot: DMA stopped in interrupt */
    {
        uint32_t sum=0, elapsed=0;
        uint16_t i, *pbuf;
        uint8_t ok, old_bit=logic_adc_info.adc_bit;
        HSADC_Function_Stop();
        logic_adc_info.adc_bit=ADC_GET_WIDTH10;
        adc_snapshot=1;
        HSADC_Function_Start();
        while(adc_snapshot!=2 && elapsed<10000) { Delay_Us(50); elapsed+=50; }
        ok=adc_snapshot==2;
        pbuf=(logic_adc_info.adc_ready_mask&1)?ADC_BufA:ADC_BufB;
        if(ok) for(i=0;i<1024;++i) sum+=pbuf[i]&1023u;
        HSADC_Function_Stop(); adc_snapshot=0; logic_adc_info.adc_bit=old_bit;
        /* Zero is a valid measurement; never replace it with an unrelated DATAR. */
        pLogicSend_Para->Logic_Cmd=CMD_GET_ADC_SAMPLE|CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len=3;
        pLogicSend_Para->Logic_Status=ok?0:1;
        pLogicSend_Para->Logic_Buf[0]=(sum/1024)&255;
        pLogicSend_Para->Logic_Buf[1]=(sum/1024)>>8;
        len=5;
        break;
    }
    case CMD_SET_ADC_START: /* Start ADC acquisition */
        logic_adc_info.usb_ram_over_flag = 0;
        logic_adc_info.usb_trans_flag = ENABLE;
        logic_adc_info.usb_trans_count = 0;
        logic_adc_info.adc_usb_limit = 0u;
        if(pLogic_Para->Logic_Len == 4u)
        {
            uint32_t bytes = (uint32_t)pLogic_Para->Logic_Buf[0] |
                             ((uint32_t)pLogic_Para->Logic_Buf[1] << 8) |
                             ((uint32_t)pLogic_Para->Logic_Buf[2] << 16) |
                             ((uint32_t)pLogic_Para->Logic_Buf[3] << 24);
            if(bytes != 0u)
                logic_adc_info.adc_usb_limit =
                    (bytes / (uint32_t)ADC_BUF_LEN) +
                    ((bytes % (uint32_t)ADC_BUF_LEN) != 0u ? 1u : 0u);
        }
        HSADC_Function_Start();
        pLogicSend_Para->Logic_Cmd = CMD_SET_ADC_START | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_SET_ADC_STOP: /* Stop ADC acquisition */
        logic_adc_info.usb_ram_over_flag = 0;
        HSADC_Function_Stop();
        logic_adc_info.usb_trans_flag = DISABLE;
        logic_adc_info.usb_trans_count = 0;
        logic_adc_info.adc_usb_limit = 0u;
        USB_LINK_ON();
        pLogicSend_Para->Logic_Cmd = CMD_SET_ADC_STOP | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 3;
        break;
    case CMD_GET_ADC_PARA: /* Get ADC acquisition parameters */
        pLogicSend_Para->Logic_Cmd = CMD_GET_ADC_PARA | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 3;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = logic_adc_info.adc_bit;
        pLogicSend_Para->Logic_Buf[1] = logic_adc_info.adc_div;
        len = 5;
        break;
    case CMD_GET_ADC_CHANNEL: /* Get ADC acquisition channel */
        pLogicSend_Para->Logic_Cmd = CMD_GET_ADC_CHANNEL | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 2;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = logic_adc_info.adc_channel;
        len = 4;
        break;
    case CMD_GET_VERSION: /* Get version */
        pLogicSend_Para->Logic_Cmd = CMD_GET_VERSION | CMD_RETURN_PARA;
        pLogicSend_Para->Logic_Len = 3;
        pLogicSend_Para->Logic_Status = 0x00;
        pLogicSend_Para->Logic_Buf[0] = SOFTWARE_VERSION;                       /* Software version */
        pLogicSend_Para->Logic_Buf[1] = HARDWARE_VERSION;                       /* Hardware version */
        len = 4;
        break;
    case CMD_GET_USB3_MUX:
    case CMD_SET_USB3_MUX:
    {
        USB3_MUX_DIAG diag;
        uint8_t ok = 1u;
        if(pLogic_Para->Logic_Cmd == CMD_SET_USB3_MUX)
            ok = USB3_MUX_SetMode(pLogic_Para->Logic_Buf[0]);
        USB3_MUX_GetDiag(&diag);
        USBSS_EP1_Tx_Buf[0] = pLogic_Para->Logic_Cmd | CMD_RETURN_PARA;
        USBSS_EP1_Tx_Buf[1] = 12u;
        USBSS_EP1_Tx_Buf[2] = ok ? 0u : 1u;
        USBSS_EP1_Tx_Buf[3] = 1u; /* diagnostic payload version */
        USBSS_EP1_Tx_Buf[4] = diag.mode;
        USBSS_EP1_Tx_Buf[5] = diag.state;
        USBSS_EP1_Tx_Buf[6] = (uint8_t)diag.cc1_raw;
        USBSS_EP1_Tx_Buf[7] = (uint8_t)(diag.cc1_raw >> 8);
        USBSS_EP1_Tx_Buf[8] = (uint8_t)diag.cc2_raw;
        USBSS_EP1_Tx_Buf[9] = (uint8_t)(diag.cc2_raw >> 8);
        USBSS_EP1_Tx_Buf[10] = (uint8_t)diag.cc1_mv;
        USBSS_EP1_Tx_Buf[11] = (uint8_t)(diag.cc1_mv >> 8);
        USBSS_EP1_Tx_Buf[12] = (uint8_t)diag.cc2_mv;
        USBSS_EP1_Tx_Buf[13] = (uint8_t)(diag.cc2_mv >> 8);
        len = 14u;
        break;
    }
    case CMD_SET_IAP: /* Set IAP download */
        /* IAP must remain recoverable even if a host disappeared while a
         * capture flag was still set.  Quiesce both acquisition engines here
         * instead of rejecting the command as "busy". */
        UHSIF_Start(DISABLE);
        if(logic_adc_info.adc_active != 0u)
        {
            HSADC_Function_Stop();
        }
        logic_adc_info.usb_trans_flag = DISABLE;
        logic_adc_info.usb_trans_count = 0;
        FLASH_Unlock_Fast();
        FLASH_ErasePage(CalAddr & (~(Flash_Erase_Page_Size - 1)));
        FLASH_ProgramWord(CalAddr, CheckNum);                                   // Write download flag
        FLASH->CTLR |= ((uint32_t)0x00008000);                                  // LOCK FLASH
        FLASH->CTLR |= ((uint32_t)0x00000080);                                  // LOCK CTLR
        if(*(volatile uint32_t*)CalAddr == CheckNum)
        {
            Delay_Ms(10);
            NVIC_SystemReset();
            while(1);
        }
        len = 0;
        break;
    default: /* Default: invalid command */
        pLogicSend_Para->Logic_Cmd = 0xFA;
        pLogicSend_Para->Logic_Len = 1;
        pLogicSend_Para->Logic_Status = 0x00;
        len = 2;
        break;
    }

send_response:
    if(len)
    {
        Command_Submit();
    }
}

/*********************************************************************
 * @fn      LOGIC_ADC_CMD_Send
 *
 * @brief   send Logic&adc status.
 *
 * @return  none
 */
void LOGIC_ADC_CMD_Process(void)
{
    if(Command_Service()) return;
    if((logic_adc_info.usb20_endp1_down == 0u) &&
       (logic_adc_info.usb30_endp1_down == 0u)) return;
    command_rx_held = 1u;
    LOGIC_ADC_CMD_ProcessPending();
    if(command_tx_busy == 0u) Command_Release_RX();
}

void LOGIC_ADC_CMD_Send(uint8_t cmd, uint8_t status)
{
    pLogicSend_Para->Logic_Cmd = cmd | CMD_RETURN_PARA;
    pLogicSend_Para->Logic_Len = 1;
    pLogicSend_Para->Logic_Status = status;
    Command_Submit();
}
/*********************************************************************
 * @fn      LOGIC_UHSIF_Main_Process
 *
 * @brief   trans Logic data.
 *
 * @return  none
 */
void LOGIC_UHSIF_Main_Process(void)
{
    if(Instrument_Busy()) return;
    uint16_t len, temp, temp1;
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
    {    // USB3.0 connected
        if((logic_adc_info.usb30_endp2_up == USB_SUCESEE_FLAG) && logic_adc_info.usb_endp2uhsif_total)
        {                                                                                           // Buffer has data, USB can send
            logic_adc_info.usb30_endp2_up = USB_BUSY_FLAG;                                          // Set USB3.0 endpoint 2 busy
            if((logic_adc_info.usb_endp2_up_count & 0x01) == 0x00)
            {
                USBSSD->EP2_TX.UEP_TX_DMA = (uint32_t)logic_adc_info.pusb_endp2_th0_up;
            }
            else
            {
                USBSSD->EP2_TX.UEP_TX_DMA = (uint32_t)logic_adc_info.pusb_endp2_th1_up;
            }
            len = logic_adc_info.usb_uhsif_reclen_buf[logic_adc_info.usb_endp2_up_count];           // Get valid data length from DRAM
            logic_adc_info.usb_uhsif_reclen_buf[logic_adc_info.usb_endp2_up_count] = 0;             // Default clear data length
            temp = len % DEF_USB_EP2_SS_SIZE;
            temp1 = len / DEF_USB_EP2_SS_SIZE;
            if(temp)
            {
                temp1 += 1;
            }
            else
            {
                temp = DEF_USB_EP2_SS_SIZE;
            }
            USBSSD->EP2_TX.UEP_TX_CHAIN_LEN = temp;
            USBSSD->EP2_TX.UEP_TX_CHAIN_EXP_NUMP = temp1;
            if((logic_adc_info.usb_endp2_up_count & 0x01) == 0x00)
            {
                logic_adc_info.pusb_endp2_th0_up += len;                                            // Buffer pointer advance
            }
            else
            {
                logic_adc_info.pusb_endp2_th1_up += len;                                            // Buffer pointer advance
            }
            logic_adc_info.usb_endp2_up_count++;
            __asm("fence");
            __asm("fence");
            if(logic_adc_info.usb_endp2_up_count == (DEF_UHSIF_TXBUF_CNT * 2 - 1))
            {    // Pointer wrap to buffer head
                logic_adc_info.pusb_endp2_th0_up = USBSS_EP2_Tx_Th0_Buf;
            }
            else if(logic_adc_info.usb_endp2_up_count == DEF_UHSIF_TXBUF_CNT * 2)
            {    // Pointer wrap to buffer head
                logic_adc_info.pusb_endp2_th1_up = USBSS_EP2_Tx_Th1_Buf;
                logic_adc_info.usb_endp2_up_count = 0;
            }
            __asm("fence");
            NVIC_DisableIRQ(USBSS_IRQn);
            NVIC_DisableIRQ(UHSIF_IRQn);
            logic_adc_info.usb_endp2uhsif_total--;
            NVIC_EnableIRQ(UHSIF_IRQn);
            NVIC_EnableIRQ(USBSS_IRQn);
            __asm("fence");
        }
    }
    else if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {
        if((logic_adc_info.usb20_endp2_up == USB_SUCESEE_FLAG) && logic_adc_info.usb_endp2uhsif_total)
        {    // Buffer has data, USB can send

            logic_adc_info.usb20_endp2_up = USB_BUSY_FLAG;
            if((logic_adc_info.usb_endp2_up_count & 0x01) == 0x00)
            {
                USBHSD->UEP2_TX_DMA = (uint32_t)logic_adc_info.pusb_endp2_th0_up;
            }
            else
            {
                USBHSD->UEP2_TX_DMA = (uint32_t)logic_adc_info.pusb_endp2_th1_up;
            }
            len = logic_adc_info.usb_uhsif_reclen_buf[logic_adc_info.usb_endp2_up_count];           // Get valid data length from DRAM
            logic_adc_info.usb_uhsif_reclen_buf[logic_adc_info.usb_endp2_up_count] = 0;             // Default clear data length
            USBHSD->UEP2_TX_LEN = len;
            USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
            if((logic_adc_info.usb_endp2_up_count & 0x01) == 0x00)
            {
                logic_adc_info.pusb_endp2_th0_up += len;                                            // Buffer pointer advance
            }
            else
            {
                logic_adc_info.pusb_endp2_th1_up += len;                                            // Buffer pointer advance
            }
            logic_adc_info.usb_endp2_up_count++;
            __asm("fence");
            if(logic_adc_info.usb_endp2_up_count == (DEF_UHSIF_TXBUF_CNT * 2 - 1))
            {    // Pointer wrap to buffer head
                logic_adc_info.pusb_endp2_th0_up = USBSS_EP2_Tx_Th0_Buf;
            }
            else if(logic_adc_info.usb_endp2_up_count == DEF_UHSIF_TXBUF_CNT * 2)
            {    // Pointer wrap to buffer head
                logic_adc_info.pusb_endp2_th1_up = USBSS_EP2_Tx_Th1_Buf;
                logic_adc_info.usb_endp2_up_count = 0;
            }
            __asm("fence");
            NVIC_DisableIRQ(USBHS_IRQn);
            NVIC_DisableIRQ(UHSIF_IRQn);
            logic_adc_info.usb_endp2uhsif_total--;
            NVIC_EnableIRQ(UHSIF_IRQn);
            NVIC_EnableIRQ(USBHS_IRQn);
            __asm("fence");
        }
    }
}

/*********************************************************************
 * @fn      HSADC_IRQHandler
 *
 * @brief   HSADC Interrupt Service Function.
 *
 * @return  none
 */
void HSADC_IRQHandler(void)
{
    if(HSADC_GetITStatus(HSADC_IT_DMAEnd))
    {
        uint8_t ready_bit;

        if((HSADC->STATR & 0x10))
        {    // Buffer A ready to send; Buffer B receiving ADC
            HSADC->ADDR0 = (uint32_t)ADC_BufA;
            ready_bit = 0x01u;
            logic_adc_info.adc_flag = 0u;
        }
        else
        {
            HSADC->ADDR1 = (uint32_t)ADC_BufB;                                                      // Buffer B ready to send; Buffer A receiving ADC
            ready_bit = 0x02u;
            logic_adc_info.adc_flag = 1u;
        }
        /* Keep the streaming path compatible with the original firmware:
         * completed DMA buffers are consumed in strict ping-pong order.  The
         * ready-mask implementation could select a buffer that the HSADC had
         * already started refilling, which left EP3 permanently idle on USB2. */
        if(logic_adc_info.adc_rec_total < 0xffu)
            logic_adc_info.adc_rec_total++;
        logic_adc_info.adc_dma_count++;
        HSADC_ClearITPendingBit(HSADC_IT_DMAEnd);
        if(adc_snapshot==1) {
            logic_adc_info.adc_ready_mask = ready_bit;
            HSADC_SoftwareStartConvCmd(DISABLE);
            HSADC_DMACmd(DISABLE);
            HSADC_Cmd(DISABLE); HSADC_ITConfig(HSADC_IT_DMAEnd,DISABLE);
            __asm volatile("fence" ::: "memory");
            adc_snapshot=2; /* completed buffer immutable before foreground reads */
        }
    }
}

static void ADC_ConsumeBuffer(void)
{
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
        NVIC_DisableIRQ(USBSS_IRQn);
    else
        NVIC_DisableIRQ(USBHS_IRQn);
    NVIC_DisableIRQ(HSADC_IRQn);
    if(logic_adc_info.adc_rec_total != 0u)
        logic_adc_info.adc_rec_total--;
    if(logic_adc_info.adc_active != 0u)
        NVIC_EnableIRQ(HSADC_IRQn);
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
        NVIC_EnableIRQ(USBSS_IRQn);
    else
        NVIC_EnableIRQ(USBHS_IRQn);
}

static void ADC_USB2_Transfer_Recover(void)
{
    if((logic_adc_info.usb_status != USB_U2_CONNECT) ||
       (logic_adc_info.usb20_endp3_up != USB_BUSY_FLAG) ||
       ((uint32_t)(Board_UI_Millis() - adc_ep3_tx_started) < ADC_EP3_TX_TIMEOUT_MS))
        return;

    NVIC_DisableIRQ(USBHS_IRQn);
    /* Recheck after masking the completion ISR. A completion that won the race
     * must retain ownership of the normal release path. */
    if((logic_adc_info.usb20_endp3_up == USB_BUSY_FLAG) &&
       ((uint32_t)(Board_UI_Millis() - adc_ep3_tx_started) >= ADC_EP3_TX_TIMEOUT_MS))
    {
        USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) |
                               USBHS_UEP_T_RES_NAK;
        USBHSD->UEP3_TX_LEN = 0u;
        USBHSD->UEP3_TX_CTRL &= ~USBHS_UEP_T_DONE;
        __asm volatile("fence" ::: "memory");
        logic_adc_info.usb20_endp3_up = USB_SUCESEE_FLAG;
        adc_ep3_tx_started = 0u;
    }
    NVIC_EnableIRQ(USBHS_IRQn);
}

/*********************************************************************
 * @fn      LOGIC_ADC_Main_Process
 *
 * @brief   trans ADC data.
 *
 * @return  none
 */
void LOGIC_ADC_Main_Process(void)
{
    if(Instrument_Busy() || adc_snapshot || !logic_adc_info.adc_active ||
       !logic_adc_info.usb_trans_flag) return;
    ADC_USB2_Transfer_Recover();
    if(logic_adc_info.usb_status == USB_U3_CONNECT)
    {    // Connected as USB3.0
        if((logic_adc_info.usb30_endp3_up == USB_SUCESEE_FLAG) &&
           logic_adc_info.adc_rec_total &&
           ((logic_adc_info.adc_usb_limit == 0u) ||
            (logic_adc_info.adc_usb_count < logic_adc_info.adc_usb_limit)))
        {
            logic_adc_info.usb30_endp3_up = USB_BUSY_FLAG;                                          // Set USB3.0 endpoint 2 busy
            if((logic_adc_info.usb_endp3_up_count & 0x01u) == 0u)
            {    // Even: use buffer A
                USBSSD->EP3_TX.UEP_TX_DMA = (uint32_t)ADC_BufA;
            }
            else
            {    // Odd: use buffer B
                USBSSD->EP3_TX.UEP_TX_DMA = (uint32_t)ADC_BufB;
            }
            USBSSD->EP3_TX.UEP_TX_CHAIN_LEN = DEF_USB_EP2_SS_SIZE;
            USBSSD->EP3_TX.UEP_TX_CHAIN_EXP_NUMP = 16;                                              // ADC_BUF_LEN/DEF_USB_EP2_SS_SIZE
            logic_adc_info.usb_endp3_up_count++;
            logic_adc_info.adc_usb_count++;
            __asm("fence");
            ADC_ConsumeBuffer();
        }
    }
    else if(logic_adc_info.usb_status == USB_U2_CONNECT)
    {    // Connected as USB2.0
        if((logic_adc_info.usb20_endp3_up == USB_SUCESEE_FLAG) &&
           logic_adc_info.adc_rec_total &&
           ((logic_adc_info.adc_usb_limit == 0u) ||
            (logic_adc_info.adc_usb_count < logic_adc_info.adc_usb_limit)))
        {    // Buffer has data, USB can send
            logic_adc_info.usb20_endp3_up = USB_BUSY_FLAG;
            if((logic_adc_info.usb_endp3_up_count & 0x01u) == 0u)
            {    // Even: use buffer A
                USBHSD->UEP3_TX_DMA = (uint32_t)ADC_BufA;
            }
            else
            {    // Odd: use buffer B
                USBHSD->UEP3_TX_DMA = (uint32_t)ADC_BufB;
            }
            USBHSD->UEP3_TX_LEN = ADC_BUF_LEN;
            adc_ep3_tx_started = Board_UI_Millis();
            USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
            logic_adc_info.usb_endp3_up_count++;
            logic_adc_info.adc_usb_count++;
            __asm("fence");
            ADC_ConsumeBuffer();
        }
    }
}

/*********************************************************************
 * @fn      LOGIC_ADC_RAMOVER_Process
 *
 * @brief   ram over process.
 *
 * @return  none
 */
void LOGIC_ADC_RAMOVER_Process(void)
{
    if(logic_adc_info.usb_endp2uhsif_total >= 8 * 2)
    {
        DBG_PRINT("logic_over\n");
        // if( logic_adc_info.usb_ram_over_flag == 0 )
        // {
        //     LOGIC_ADC_CMD_Send( CMD_LOGIC_OVER,LOGIC_RAM_OVER );
        //     logic_adc_info.usb_ram_over_flag = 1;
        // }
    }
    if((logic_adc_info.adc_overrun_count != 0u) &&
       (logic_adc_info.usb_ram_over_flag == 0u))
    {
        DBG_PRINT("adc_over\n");
        logic_adc_info.usb_ram_over_flag = ADC_RAM_OVER;
    }
}

/*********************************************************************
 * @fn      Get_Flash_Size
 *
 * @brief   get flash size init 8K.
 *
 * @return  none
 */
void Get_Flash_Size(void)
{
    if(((*(volatile uint32_t*)FLASH_CFGR0_BASE) & (1 << 28)) != 0)
    {
        Flash_Erase_Page_Size = Size_8KB;
    }
    else
    {
        Flash_Erase_Page_Size = Size_4KB;
    }
}

/*********************************************************************
 * @fn      IAP_JUMP_Process
 *
 * @brief   reset mcu input iap.
 *
 * @return  none
 */
void IAP_JUMP_Process(void)
{
    if(*(volatile uint32_t*)CalAddr == CheckNum)
    {
        DBG_PRINT("reset\n");
        Delay_Ms(10);
        NVIC_SystemReset();
        while(1);
    }
}



/*********************************************************************
 * @fn      TIM1_INT_Init
 *
 * @brief   Initializes TIM1 output .
 *
 * @param   none.
 *
 * @return  none
 */
void TIM1_INT_Init( u16 arr, u16 psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure={0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_TIM1, ENABLE );

    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit( TIM1, &TIM_TimeBaseInitStructure);

    TIM_ClearITPendingBit( TIM1, TIM_IT_Update );

	NVIC_SetPriority(TIM1_UP_IRQn,3);
	NVIC_EnableIRQ(TIM1_UP_IRQn);

    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);
    TIM_Cmd( TIM1, ENABLE );
}


/*********************************************************************
 * @fn      TIM1_UP_IRQHandler
 *
 * @brief   This function handles TIM1 UP exception.
 *
 *
 * @return  none
 */
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM1, TIM_IT_Update)==SET)
    {
        Board_UI_Tick();
        if( logic_adc_info.usb_trans_flag ){
            logic_adc_info.usb_trans_count++;
            if( logic_adc_info.usb_trans_count &0x01 ){
                USB_LINK_OFF( );
            }
            else{
                USB_LINK_ON( );
            }
        }
    }
    TIM_ClearITPendingBit( TIM1, TIM_IT_Update );
}


/*********************************************************************
 * @fn      Led_Time_Init
 *
 * @brief   This function  TIM1 Init.
 *
 *
 * @return  none
 */
void Led_Time_Init( void )
{
    TIM1_INT_Init(4999, (SystemClock / 1000000u) - 1u);
    TIM_Cmd( TIM1, ENABLE );
}
