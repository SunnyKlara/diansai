/**
 * @file    oled.c
 * @brief   OLED 占位（赛场实操时粘贴成熟字模库）
 */

#include "oled.h"
#include <stdio.h>

void OLED_Init(void) { /* TODO_HAL */ }
void OLED_Clear(void) { /* TODO_HAL */ }
void OLED_ShowString(uint8_t x, uint8_t y, const char* str)
{
    (void)x; (void)y; (void)str;
    /* TODO_HAL */
}
void OLED_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%6.2f%s", label, (double)val, unit);
    OLED_ShowString(x, y, buf);
}
