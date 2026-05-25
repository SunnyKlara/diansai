/**
 * @file    spwm.h
 * @brief   SPWM 正弦表 + 角度推进（与硬件解耦）
 *
 *  设计：
 *      - 不依赖 STM32 HAL，可直接在 PC 端单元测试
 *      - 输出为 [-1, +1] 的正弦值，由 main 转换成 PWM 占空比
 *      - 支持运行时改频率（调整正弦表长度）
 */

#ifndef __SPWM_H
#define __SPWM_H

#include <stdint.h>

/**
 * @brief 初始化正弦表
 * @param table_len  表长度 = 开关频率 / 输出频率（典型 400 = 20kHz / 50Hz）
 */
void  SPWM_Init(uint16_t table_len);

/**
 * @brief 获取下一个正弦值（在 PWM 中断中调用）
 * @return [-1.0f, +1.0f]
 */
float SPWM_GetNextValue(void);

/**
 * @brief 运行时改输出频率（开关频率不变，改正弦表长度）
 */
void  SPWM_SetFrequency(float freq_hz);

/**
 * @brief 重置相位（从 0 开始）
 */
void  SPWM_Reset(void);

#endif /* __SPWM_H */
