/**
 * @file    lcd_ht1621.h
 * @brief   HT1621 段码 LCD（极低功耗显示，~ 0.5 mW）
 *
 *  接口：3 线 SPI（CS / WR / DATA），软件位拨即可，不占用硬件 SPI。
 *  显示能力：32×4 = 128 段，足够做 5 个 4 位数字 + 单位标识。
 */

#ifndef __LCD_HT1621_H
#define __LCD_HT1621_H

#include <stdint.h>

void LCD_Init(void);
void LCD_Clear(void);

/**
 * @brief 在指定数字位置显示浮点数
 * @param slot   数字位（0~4）
 * @param value  数值（自动选小数点位数）
 * @param unit   单位标识（"V"/"A"/"W"/"PF"/"%"）
 */
void LCD_ShowFloat(uint8_t slot, float value, const char *unit);

/**
 * @brief 进入显示低功耗模式（保留显示，关掉刷新逻辑）
 */
void LCD_LowPowerMode(void);

#endif /* __LCD_HT1621_H */
