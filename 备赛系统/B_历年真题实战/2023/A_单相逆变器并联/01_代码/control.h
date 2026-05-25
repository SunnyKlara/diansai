#ifndef __CONTROL_H
#define __CONTROL_H

#include "stm32g4xx_hal.h"

void Control_Init(float vref);
float Control_VoltageLoop(float vout_rms);
void Control_SetVref(float vref);

#endif
