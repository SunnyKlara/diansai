#ifndef __SPWM_H
#define __SPWM_H

#include "stm32g4xx_hal.h"
#include <math.h>

void SPWM_Init(uint16_t table_len);
float SPWM_GetNextValue(void);
void SPWM_SetFrequency(float freq_hz);
void SPWM_Reset(void);

#endif
