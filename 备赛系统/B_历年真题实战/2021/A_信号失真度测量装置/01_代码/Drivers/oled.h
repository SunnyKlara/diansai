/**
 * @file    oled.h
 * @brief   SSD1306 OLED 接口（I2C UCB0，I2C 地址 0x3C）
 */

#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowString(uint8_t x, uint8_t y, const char* str);

/**
 * @brief 在屏幕指定区域绘制波形（一周期数据）
 * @param data    int16_t 采样数据
 * @param len     数据长度
 * @param y_start 起始 Y（0~63）
 * @param height  高度（像素）
 */
void OLED_DrawWaveform(int16_t* data, uint16_t len, uint8_t y_start, uint8_t height);

/**
 * @brief 在屏幕指定区域绘制柱状图（用于谐波归一化幅值）
 */
void OLED_DrawBarChart(float* values, uint8_t count, uint8_t y_start, uint8_t height);

#endif /* __OLED_H */
