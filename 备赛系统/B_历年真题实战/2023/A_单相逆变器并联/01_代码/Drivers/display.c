/**
 * @file    display.c
 * @brief   SSD1306 OLED 实现（占位，赛场可粘贴成熟字模库）
 */

#include "display.h"
#include "../config.h"
#include <stdio.h>

/* TODO_HAL: #include "stm32g4xx_hal.h" */
/* TODO_HAL: extern I2C_HandleTypeDef hi2c1; */

void Display_Init(void)
{
    /* TODO_HAL:
     *   - 等待 OLED 上电稳定 (100ms)
     *   - 发送 SSD1306 初始化命令序列（28 字节）
     *   - 清屏
     */
}

void Display_Clear(void)
{
    /* TODO_HAL: 把 8 页显存全写 0 */
}

void Display_ShowString(uint8_t x, uint8_t y, const char* str)
{
    (void)x; (void)y; (void)str;
    /* TODO_HAL: 查 6×8 字模 → 写显存 */
}

void Display_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit)
{
    char buf[22];
    snprintf(buf, sizeof(buf), "%s%6.2f%s", label, (double)val, unit);
    Display_ShowString(x, y, buf);
}
