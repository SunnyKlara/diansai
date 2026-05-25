/**
 * @file    adc_dual.h
 * @brief   MSP430 ADC12_B 双通道（电压+电流）同步采样 + DMA
 *
 *  采样模式：
 *      Timer_A 触发 → ADC12 顺序扫描 A0(电压) + A1(电流) → DMA 写双缓冲
 *      DMA 满中断 → 唤醒主循环做计算
 *
 *  数据格式（DMA 缓冲）：
 *      [V0, I0, V1, I1, V2, I2, ...] 交错存储
 *      上层 ADCDual_Convert() 拆分为两路 + 物理量转换
 */

#ifndef __ADC_DUAL_H
#define __ADC_DUAL_H

#include <stdint.h>
#include "../config.h"

typedef struct {
    float v_samples[DFT_N];     /* 电压物理量数组 */
    float i_samples[DFT_N];     /* 电流物理量数组 */
} ADCConvertedFrame;

void ADCDual_Init(void);

/** @brief 启动一次完整采集（DFT_N 个 V+I 对，约 100ms）*/
void ADCDual_StartOnce(void);

/** @brief 查询是否完成 */
uint8_t ADCDual_IsDone(void);

/** @brief 把 DMA 原始数据解码为 ADCConvertedFrame
 *  应用电压 / 电流校准系数（来自 calibration.h，目前用 config.h 初值）
 */
void ADCDual_GetFrame(ADCConvertedFrame *frame);

/** @brief 强制停止（故障 / 复位时调用）*/
void ADCDual_Stop(void);

#endif /* __ADC_DUAL_H */
