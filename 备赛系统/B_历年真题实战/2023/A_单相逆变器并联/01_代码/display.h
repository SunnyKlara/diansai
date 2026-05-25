#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "stm32g4xx_hal.h"

void Display_Init(I2C_HandleTypeDef* hi2c);
void Display_Clear(void);
void Display_ShowString(uint8_t x, uint8_t y, const char* str);
void Display_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit);

#endif
