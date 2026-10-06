/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.h
* Author             : Q2H2
* Version            : V1.0.0
* Date               : 2026/04/29
* Description        : This file contains all the functions prototypes for the 
*                      hardware firmware library.
********************************************************************************/
#ifndef __HARDWARE_H
#define __HARDWARE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "ch32h417.h"
#include "debug.h"

#define DEF_TX_CHAIN_TIMEOUT 1000000

#define DEF_UHSIF_TXBUF_CNT  8
#define DEF_UHSIF_RXBUF_CNT  8
typedef struct
{
    volatile uint32_t adr;
    volatile uint32_t len;
} pack_t;

typedef struct
{
    volatile uint8_t load;
    volatile uint8_t deal;
    volatile uint8_t total;
    volatile uint8_t stop;
} comm_t;

extern pack_t UHSIF_WR_Pack[DEF_UHSIF_TXBUF_CNT];
extern comm_t UHSIF_WR_COMM;
extern pack_t UHSIF_RD_Pack[DEF_UHSIF_RXBUF_CNT];
extern comm_t UHSIF_RD_COMM;

extern void UHSIF_Init(void);
extern void UHSIF_Para_Init(void);
extern void USB3_MUX_Init(void);
extern void USB3_MUX_Update(void);

#define USB3_MUX_MODE_AUTO      0u
#define USB3_MUX_MODE_PORT_A    1u
#define USB3_MUX_MODE_PORT_B    2u
#define USB3_MUX_MODE_DISABLED  3u

typedef struct
{
    uint16_t cc1_raw;
    uint16_t cc2_raw;
    uint16_t cc1_mv;
    uint16_t cc2_mv;
    uint8_t mode;
    uint8_t state;
} USB3_MUX_DIAG;

/* state bits: 0 CC1 active, 1 CC2 active, 2 ADC ready, 3 switch enabled,
 * 4 SEL output, 5 orientation valid, 6 applied port (0=A, 1=B). */
extern uint8_t USB3_MUX_SetMode(uint8_t mode);
extern void USB3_MUX_GetDiag(USB3_MUX_DIAG *diag);
extern void Hardware(void);
extern void UART_Proc(void);
extern void USART1_Init(uint32_t baudrate);

#ifdef __cplusplus
}
#endif

#endif
