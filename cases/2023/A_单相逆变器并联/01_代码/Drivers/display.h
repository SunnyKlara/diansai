/**
 * @file    display.h
 * @brief   SSD1306 OLED 显示
 */

#ifndef __DISPLAY_H
#define __DISPLAY_H

#include <stdint.h>

void Display_Init(void);
void Display_Clear(void);
void Display_ShowString(uint8_t x, uint8_t y, const char* str);
void Display_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit);

#endif /* __DISPLAY_H */
