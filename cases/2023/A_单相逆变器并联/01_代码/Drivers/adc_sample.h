/**
 * @file    adc_sample.h
 * @brief   双通道 ADC + DMA 采样 + RMS 计算
 *
 *  采样模型：
 *      TIM1 TRGO 触发 → ADC1 顺序扫描（电压 → 电流）→ DMA 写双缓冲
 *      DMA 满中断 → 计算 Vrms / Irms → 喂给电压闭环
 *
 *  数据格式：
 *      DMA 缓冲：[V0, I0, V1, I1, V2, I2, ...] 交错
 *      上层调用 ADC_Sample_CalcVrms / Irms 拿 RMS 物理量
 */

#ifndef __ADC_SAMPLE_H
#define __ADC_SAMPLE_H

#include <stdint.h>
#include "../config.h"

/**
 * @brief 初始化（占位，HAL 句柄通过弱符号 / 全局变量传入）
 */
void  ADC_Sample_Init(void);

/**
 * @brief 启动 DMA 持续采样
 */
void  ADC_Sample_Start(void);

/**
 * @brief 停止采样
 */
void  ADC_Sample_Stop(void);

/**
 * @brief 计算电压 RMS（V），从最近一帧 DMA 数据提取
 */
float ADC_Sample_CalcVrms(void);

/**
 * @brief 计算电流 RMS（A）
 */
float ADC_Sample_CalcIrms(void);

#endif /* __ADC_SAMPLE_H */
