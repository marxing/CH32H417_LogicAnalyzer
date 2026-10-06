/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2025/05/07
 * Description        : Main program body for V5F.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "hardware.h"

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
	SystemAndCoreClockUpdate();
	Delay_Init();
	USART_Printf_Init(921600);
	DBG_PRINT("V5F SystemCoreClk:%d\r\n", SystemCoreClock);
	/* Keep this before Hardware(): it distinguishes power/pin/watchdog/software
	 * resets from faults occurring while optional peripherals are probed. */
	DBG_PRINT("ResetFlags:%08lx PIN=%d POR=%d SFT=%d IWDG=%d WWDG=%d LOCKUP=%d\r\n",
		(unsigned long)RCC->RSTSCKR,
		RCC_GetFlagStatus(RCC_FLAG_PINRST), RCC_GetFlagStatus(RCC_FLAG_PORRST),
		RCC_GetFlagStatus(RCC_FLAG_SFTRST), RCC_GetFlagStatus(RCC_FLAG_IWDGRST),
		RCC_GetFlagStatus(RCC_FLAG_WWDGRST), RCC_GetFlagStatus(RCC_FLAG_LKUPRSTF));
	RCC_ClearFlag();
#if (Run_Core == Run_Core_V3FandV5F)
	HSEM_FastTake(HSEM_ID0);
	HSEM_ReleaseOneSem(HSEM_ID0, 0);

#elif (Run_Core == Run_Core_V3F)

#elif (Run_Core == Run_Core_V5F)
	Hardware();
#endif

	while (1)
	{
	}
}
