/**
 * @file    adc_3phase.h
 * @brief   三相 ADC 同步采样（电压 × 3 + 电流 × 3 + 母线 × 1）
 *
 *  STM32G474 的 ADC1 + ADC2 双重采样：
 *    ADC1：CH1=Va, CH2=Vb, CH3=Vc, CH4=Vbus    （4 通道）
 *    ADC2：CH1=Ia, CH2=Ib, CH3=Ic              （3 通道）
 *
 *  触发：TIM1 TRGO（每开关周期采一次，与 PWM 中心对齐采样）
 *  传输：DMA 循环模式，缓冲区 2× 通道数（双缓冲）
 *
 *  完成回调：每次缓冲一半 / 全满时调用 ADC3Phase_OnSampleReady()，
 *           上层在该回调中：
 *             1. 把 raw → 物理量
 *             2. 喂入 RMSMeter
 *             3. 喂入 FeedbackCtrl（如启用同步整流）
 */

#ifndef __ADC_3PHASE_H
#define __ADC_3PHASE_H

#include <stdint.h>

typedef struct {
    uint16_t raw_v[3];     /* Va, Vb, Vc */
    uint16_t raw_vbus;
    uint16_t raw_i[3];     /* Ia, Ib, Ic */
} ADCSample;

typedef struct {
    float v_phase[3];      /* 相电压（去偏置后）V */
    float v_line[3];       /* 线电压 Vab/Vbc/Vca V */
    float vbus;            /* 母线电压 V */
    float i_phase[3];      /* 三相电流 A */
} ADCConverted;

/**
 * @brief 初始化
 */
void ADC3Phase_Init(void);

/**
 * @brief 启动持续采样（在 main 状态机进入 RUN 后调用）
 */
void ADC3Phase_Start(void);

/**
 * @brief 停止采样
 */
void ADC3Phase_Stop(void);

/**
 * @brief 把原始 ADC 值转为物理量
 */
void ADC3Phase_Convert(const ADCSample *raw, ADCConverted *out);

/**
 * @brief 每次 DMA 半 / 全 完成时由中断回调调用（弱符号，由 main 实现）
 *  上层在此处做 RMS 累加、闭环更新、SR 决策。
 */
void ADC3Phase_OnSampleReady(const ADCConverted *sample);

#endif /* __ADC_3PHASE_H */
