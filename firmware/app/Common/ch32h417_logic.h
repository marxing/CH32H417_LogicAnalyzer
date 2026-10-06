/********************************** (C) COPYRIGHT  *******************************
 * File Name          : ch32h417_logic.h
 * Author             : Q2H2
 * Version            : V1.0.0
 * Date               : 2026/05/18
 * Description        : Header file for ch32h417_logic.c
 ********************************************************************************/
#ifndef __CH32H417_LOGIC_H
#define __CH32H417_LOGIC_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "ch32h417.h"
#include "debug.h"

#define pLogic_Para         ((PLOGIC_PARA)USBSS_EP1_Rx_Buf)     /* Logic analyzer RX command buffer */
#define pLogicSend_Para     ((PSENDLOGIC_PARA)USBSS_EP1_Tx_Buf) /* Logic analyzer TX command buffer */

/* Logic analyzer command codes */
#define CMD_SET_LOGIC_PARA  0xA0                                                /* Set acquisition parameters */
#define CMD_SET_LOGIC_LEVEL 0xA1                                                /* Set acquisition trigger level */
#define CMD_SET_START       0xA2                                                /* Start acquisition */
#define CMD_SET_STOP        0xA3                                                /* Stop acquisition */
#define CMD_GET_LOGIC_PARA  0xA4                                                /* Get acquisition parameters */
#define CMD_GET_LOGIC_LEVEL 0xA5                                                /* Get acquisition trigger level */

/* ADC command codes */
#define CMD_SET_ADC_PARA    0xA6                                                /* Set ADC acquisition parameters */
#define CMD_SET_ADC_CHANNEL 0xA7                                                /* Set ADC acquisition channel */
#define CMD_SET_ADC_START   0xA8                                                /* Start ADC acquisition */
#define CMD_SET_ADC_STOP    0xA9                                                /* Stop ADC acquisition */
#define CMD_GET_ADC_PARA    0xAA                                                /* Get ADC acquisition parameters */
#define CMD_GET_ADC_CHANNEL 0xAB                                                /* Get ADC acquisition channel */
#define CMD_LOGIC_OVER      0xAD                                                /* Logic analyzer or ADC buffer overflow. status=1: logic overflow, status=2: ADC overflow */
#define CMD_GET_VERSION     0xAC                                                /* Get firmware version */
#define CMD_SET_IAP         0xAE                                                /* IAP upgrade command */
#define CMD_SET_ADC_RANGE   0xAF                                                /* Set AFE range: payload channel, range */
#define CMD_GET_ADC_RANGE   0xB0                                                /* Get AFE ranges */
#define CMD_GET_ADC_SAMPLE  0xB1                                                /* Calibration: one-shot averaged ADC read */
#define CMD_GET_USB3_MUX    0xC3                                                /* Read CC and USB3 MUX diagnostics */
#define CMD_SET_USB3_MUX    0xC4                                                /* Set MUX mode: auto/A/B/disabled */

#define SOFTWARE_VERSION    0x33

void Command_Transport_Reset(void);
void Command_Tx_Complete(void);
#define HARDWARE_VERSION    0x01

#define LOGIC_RAM_OVER      0x01                                                /* Logic RAM overflow */
#define ADC_RAM_OVER        0x02                                                /* ADC RAM overflow */

#define CMD_RETURN_PARA     0x10                                                /* Command response */

#define ADC_CHANNEL0        0x00
#define ADC_CHANNEL1        0x01

/* Board-level analog connections from SCH_Schematic-pre0v2_1_2026-09-12:
 * CH2_AFE_OUT -> PC2 / HSADC2, CH1_AFE_OUT -> PC3 / HSADC3. */
#define BOARD_ADC_GPIO_PORT          GPIOC
#define BOARD_ADC_CH1_GPIO_PIN       GPIO_Pin_3
#define BOARD_ADC_CH2_GPIO_PIN       GPIO_Pin_2
#define BOARD_ADC_CH1_HSADC_CHANNEL  HSADC_Channel_3
#define BOARD_ADC_CH2_HSADC_CHANNEL  HSADC_Channel_2

/* TPG4735 range select lines. S1:S0 selects 6.98k/28k/140k/280k. */
#define BOARD_AFE_CH1_S0_PORT        GPIOE
#define BOARD_AFE_CH1_S0_PIN         GPIO_Pin_2
#define BOARD_AFE_CH1_S1_PORT        GPIOC
#define BOARD_AFE_CH1_S1_PIN         GPIO_Pin_13
#define BOARD_AFE_CH2_S0_PORT        GPIOC
#define BOARD_AFE_CH2_S0_PIN         GPIO_Pin_14
#define BOARD_AFE_CH2_S1_PORT        GPIOC
#define BOARD_AFE_CH2_S1_PIN         GPIO_Pin_15

#define ADC_RANGE_WIDE               0x00u                                      /* 6.98k, lowest gain */
#define ADC_RANGE_MEDIUM             0x01u                                      /* 28k */
#define ADC_RANGE_NARROW             0x02u                                      /* 140k */
#define ADC_RANGE_SENSITIVE          0x03u                                      /* 280k, highest gain */
#define ADC_RANGE_COUNT              4u
#define ADC_HSADC_MIN_DIV            3u                                         /* 400MHz/(3+1)/5 = 20MSPS */
#define ADC_HSADC_MAX_DIV            0x3fu
#define ADC_AFE_SETTLE_US            20u

#define ADC_GET_WIDTH8      0x08                                                /* ADC 8-bit sampling */
#define ADC_GET_WIDTH10     0x0a                                                /* ADC 10-bit sampling */

#define USB_NO_CONNECT      0x00                                                /* USB disconnected */
#define USB_U2_CONNECT      0x01                                                /* USB 2.0 connected */
#define USB_U3_CONNECT      0x02                                                /* USB 3.0 connected */

#define USB_SUCESEE_FLAG    0x00
#define USB_BUSY_FLAG       0x01

#define ADC_BUF_LEN         (16 * 1024)                                         /* ADC buffer size */

#define Size_256B           0x100
#define Size_4KB            0x1000
#define Size_8KB            0x2000
#define CalAddr             (0x08078000 - 4)                                    /* IAP flag address in flash */
#define CheckNum            (0x5aa55aa5)                                        /* IAP flag check value */


/* PB3/PB4 are CC1/CC2-related nets on the new USB-C schematic. The old
 * firmware LED macros must not drive them. Keep compatibility for callers. */
#define USB_RUN_OFF()  do { } while(0)
#define USB_RUN_ON()   do { } while(0)
#define USB_LINK_OFF() do { } while(0)
#define USB_LINK_ON()  do { } while(0)


typedef __attribute__((aligned(1))) struct _LOGIC_param
{
    uint8_t Logic_Cmd;                                                          /* Command code */
    uint8_t Logic_Len;                                                          /* Payload length */
    uint8_t Logic_Buf[5];                                                       /* Payload data */
} LOGIC_PARA, *PLOGIC_PARA;

typedef __attribute__((aligned(1))) struct _SEND_LOGIC_param
{
    uint8_t Logic_Cmd;                                                          /* Command code */
    uint8_t Logic_Len;                                                          /* Payload length */
    uint8_t Logic_Status;                                                       /* Status */
    uint8_t Logic_Buf[5];                                                       /* Payload data */
} SENDLOGIC_PARA, *PSENDLOGIC_PARA;

typedef __attribute__((aligned(1))) struct _LOGIC_ADC_param
{
    /* Logic acquisition parameters */
    uint8_t logic_bit;                                                          /* Sample bit width */
    uint8_t logic_sys;                                                          /* Sampling frequency */
    uint8_t uhsif_div;                                                          /* Clock divider */
    uint16_t logic_level;                                                       /* Trigger level */
    /* ADC acquisition parameters */
    uint8_t adc_bit;                                                            /* ADC sample bit width: 0x08=8-bit, 0x0a=10-bit */
    uint8_t adc_div;                                                            /* ADC clock divider: 0=no division, 1=/2 ... 0x3f=/64 */
    uint8_t adc_channel;                                                        /* ADC channel: 0=ch0, 1=ch1 */
    uint8_t adc_flag;                                                           /* ADC buffer flag: 0=buf0, 1=buf1 */
    /* USB status - logic */
    uint8_t volatile usb_status;                                                /* USB connection: 0x01=USB3.0, 0x02=USB2.0 */
    uint8_t volatile usb30_endp1_down;                                          /* USB3.0 EP1 RX flag */
    uint8_t volatile usb20_endp1_down;                                          /* USB2.0 EP1 RX flag */
    uint8_t volatile usb_ram_over_flag;                                         /* USB RAM overflow flag */
    uint8_t* pusb_endp2_th0_up;                                                 /* USB TX buffer pointer */
    uint8_t* pusb_endp2_th1_up;                                                 /* USB TX buffer pointer */
    uint8_t volatile usb30_endp2_up;                                            /* USB3.0 EP2 TX flag (logic upload) */
    uint8_t volatile usb20_endp2_up;                                            /* USB2.0 EP2 TX flag (logic upload) */
    uint8_t volatile usb_endp2_up_count;                                        /* USB upload count */
    uint8_t volatile usb_endp2uhsif_total;                                      /* UHSIF RX + USB TX total count */
    uint8_t volatile usb_uhsif_Th0_count;                                       /* UHSIF thread 0 RX count */
    uint8_t volatile usb_uhsif_Th1_count;                                       /* UHSIF thread 1 RX count */
    uint16_t usb_uhsif_reclen_buf[255];                                         /* UHSIF received data length buffer */
    /* USB status - ADC */
    uint8_t volatile usb30_endp3_up;                                            /* USB3.0 EP3 TX flag (ADC upload) */
    uint8_t volatile usb20_endp3_up;                                            /* USB2.0 EP3 TX flag (ADC upload) */
    uint8_t volatile adc_rec_total;                                             /* ADC total sample count */
    uint8_t volatile usb_endp3_up_count;                                        /* USB upload count */

    /* LED status - LED*/
    uint16_t volatile usb_trans_count;                                            /* USB3.0 EP3 TX flag (ADC upload) */
    uint8_t volatile usb_trans_flag;

    /* Keep new fields at the end so legacy USB objects retain the original
     * offsets even when an incremental IDE build misses this header change. */
    uint8_t adc_range[2];                                                       /* AFE ranges for CH1 and CH2 */
    uint8_t volatile adc_ready_mask;                                            /* bit0=buffer A ready, bit1=buffer B ready */
    uint8_t volatile adc_active;                                                /* HSADC conversion running */
    uint16_t volatile adc_overrun_count;                                        /* DMA buffer overwrite count */
    uint32_t volatile adc_dma_count;                                            /* completed HSADC DMA buffers */
    uint32_t volatile adc_usb_count;                                            /* buffers submitted to USB EP3 */
    uint32_t volatile adc_usb_limit;                                            /* finite EP3 buffer count; 0=continuous */
} LOGIC_ADC_PARA, *PLOGIC_ADC_PARA;

extern LOGIC_ADC_PARA logic_adc_info;                                           /* Logic and ADC parameter buffer */
extern void LOGIC_UHSIF_Main_Process(void);                                     /* Logic acquisition main process */
extern void LOGIC_ADC_Main_Process(void);                                       /* ADC acquisition main process */
extern void LOGIC_Para_Init(void);                                              /* Parameter initialization */
extern void LOGIC_ADC_CMD_Process(void);                                        /* Logic & ADC command processing */
extern void Dac_Init(void);                                                     /* DAC output initialization */
extern void LOGIC_ADC_CMD_Send(uint8_t cmd, uint8_t status);                    /* Report command status */
extern void LOGIC_ADC_RAMOVER_Process(void);                                    /* RAM overflow handler */
extern void Get_Flash_Size(void);                                               /* Get flash size for IAP upgrade */
extern void IAP_JUMP_Process(void);                                             /* Jump to IAP bootloader */
extern void HSADC_Function_Start(void);
extern void HSADC_Function_Stop(void);
extern void ADC_AFE_Init(void);
extern uint8_t ADC_AFE_SetRange(uint8_t channel, uint8_t range);
extern uint8_t ADC_AFE_GetRange(uint8_t channel);
extern void Led_Time_Init( void );

#ifdef __cplusplus
}
#endif
#endif
