/**
 * @file    oled.h
 * @brief   SSD1306 OLED 显示驱动接口
 *
 *  本模块为 main.c 引用的 OLED_Init / OLED_Clear / OLED_ShowString 提供
 *  完整的接口声明。具体的字模、I2C 写入封装在 oled.c 中。
 *
 *  屏幕：0.96" 128×64 SSD1306，I2C 地址 0x3C，I2C1 总线。
 */

#ifndef __OLED_H
#define __OLED_H

#include "stm32f4xx_hal.h"

/* SSD1306 默认 I2C 地址（写：0x78，读：0x79，HAL 用 7bit 左移 1）*/
#define OLED_I2C_ADDR       0x78

/**
 * @brief 初始化 OLED
 * @param hi2c  I2C 句柄指针（main.c 传入 &hi2c1）
 *
 *  内部完成：等待 I2C 就绪 → 发送 SSD1306 初始化序列（28 字节）→ 清屏
 */
void OLED_Init(I2C_HandleTypeDef* hi2c);

/**
 * @brief 清屏（显存全 0 + 显示全黑）
 */
void OLED_Clear(void);

/**
 * @brief 在指定位置显示字符串
 * @param x  列（0~127，按 6 像素字符宽对齐）
 * @param y  行（0~7，每行 8 像素）
 * @param str  ASCII 字符串
 *
 *  使用 6×8 字模（21 列 × 8 行 = 168 字符可见）。
 *  超出屏幕的字符自动截断，不换行。
 */
void OLED_ShowString(uint8_t x, uint8_t y, const char* str);

/**
 * @brief 刷新到屏幕（写显存到 SSD1306）
 *
 *  OLED_Clear 和 OLED_ShowString 修改的是内部显存，
 *  主程序在合适的时机调用 OLED_Refresh() 把显存写到屏幕。
 *  本接口在 OLED_Clear / OLED_ShowString 内部自动调用，
 *  外部一般不直接用。
 */
void OLED_Refresh(void);

#endif /* __OLED_H */
