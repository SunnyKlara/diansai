#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowString(uint8_t x, uint8_t y, const char* str);
void OLED_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit);

#endif
