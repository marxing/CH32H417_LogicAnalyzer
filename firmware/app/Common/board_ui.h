#ifndef __BOARD_UI_H
#define __BOARD_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void Board_UI_Init(void);
void Board_UI_Process(void);
void Board_UI_Tick(void);
uint32_t Board_UI_Millis(void);

#ifdef __cplusplus
}
#endif

#endif
