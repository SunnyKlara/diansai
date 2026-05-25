#ifndef __SI5351_H
#define __SI5351_H

#include "stm32f1xx_hal.h"

/**
 * Si5351A PLL频率合成器驱动
 * I2C地址: 0x60
 * 参考时钟: 25MHz晶振
 * 输出频率范围: 8kHz ~ 160MHz
 */

#define SI5351_ADDR  0xC0  // 7bit地址0x60左移1位

void Si5351_Init(void);

/**
 * 设置CLK输出频率
 * @param clk_num  输出通道 (0, 1, 2)
 * @param freq_hz  目标频率 (Hz)
 */
void Si5351_SetFreq(uint8_t clk_num, uint32_t freq_hz);

/**
 * 使能/禁用CLK输出
 */
void Si5351_EnableOutput(uint8_t clk_num, uint8_t enable);

#endif
