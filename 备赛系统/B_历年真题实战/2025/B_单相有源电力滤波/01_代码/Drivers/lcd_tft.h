/**
 * @file    lcd_tft.h
 * @brief   2.8" SPI TFT 显示驱动占位 —— 波形 + 谐波数据展示
 *
 * 资源映射（占位）：
 *   - SPI2 + DMA1 ST3
 *   - 320×240 / ILI9341
 *   - 5 Hz 刷新率（200ms 一帧，由 TIM3 触发）
 */

#ifndef __LCD_TFT_H__
#define __LCD_TFT_H__

#include <stdint.h>
#include "../Algorithm/harmonic_fft.h"

void lcd_tft_init(void);

/**
 * @brief 清屏 + 刷新底图框架
 */
void lcd_tft_clear(void);

/**
 * @brief 顶部状态栏：当前模式（IDLE / MEAS / APF / FAULT）
 */
void lcd_tft_show_mode(const char *mode_name);

/**
 * @brief 中间区：实时波形（uS, iL, iS = iL - iF）
 * @param us_buf, il_buf, is_buf  长度 = w 的瞬时值数组（已归一化到屏幕高度）
 * @param w 列数 = LCD 宽 / 2 = 160
 */
void lcd_tft_show_wave(const float *us_buf, const float *il_buf,
                       const float *is_buf, uint16_t w);

/**
 * @brief 底部区：谐波数据（H₁..H₅ 含有率 + THD）
 */
void lcd_tft_show_harmonic(const harmonic_result_t *r);

#endif /* __LCD_TFT_H__ */
