/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32h417_it.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
  /* The previous immediate reset made a persistent fault look like an
   * unexplained reboot loop. UART is already initialized before Hardware(). */
  DBG_PRINT("HARDFAULT V5 mcause=%08lx mepc=%08lx mtval=%08lx\r\n",
            (unsigned long)__get_MCAUSE(),
            (unsigned long)__get_MEPC(),
            (unsigned long)__get_MTVAL());
  Delay_Ms(250);
#if defined(APP_BOOT_DIAGNOSTIC) && APP_BOOT_DIAGNOSTIC
  /* Keep the first fault available to the debugger instead of rebooting. */
  __disable_irq();
#else
  NVIC_SystemReset();
#endif
  while (1)
  {
  }
}


