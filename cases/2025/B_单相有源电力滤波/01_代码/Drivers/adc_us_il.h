/**
 * @file    adc_us_il.h
 * @brief   STM32F407 双 ADC 同步采样驱动（uS / iL / iF）
 *
 * 资源映射（占位，可改）：
 *   - ADC1 IN0  ←  uS（电网电压）
 *   - ADC1 IN1  ←  iL（负载电流）
 *   - ADC2 IN2  ←  iF（APF 输出电流）
 *   - 触发     ←  TIM1 TRGO（20 kHz）
 *   - DMA1 ST0 ←  双 ADC 同步搬运
 */

#ifndef __ADC_US_IL_H__
#define __ADC_US_IL_H__

#include <stdint.h>

/**
 * @brief 单次双 ADC 同步采样输出（瞬时值，物理量已换算）
 */
typedef struct {
    float us;     /* 电网电压瞬时值（V）   */
    float il;     /* 负载电流瞬时值（A）   */
    float i_f;    /* APF 输出电流瞬时值（A）*/
} adc_sample_t;

/**
 * @brief 初始化 ADC + DMA + 同步触发
 *        在系统时钟、GPIO 初始化完成后调用一次。
 */
void adc_us_il_init(void);

/**
 * @brief 启动 ADC（必须先调用 init）：开 DMA + 启动触发定时器。
 */
void adc_us_il_start(void);

/**
 * @brief 停止 ADC（关 DMA + 关触发）。
 */
void adc_us_il_stop(void);

/**
 * @brief 软件零点自校（开机时调一次）：
 *        让上层在无信号状态下采 N 点平均，写入校准 offset。
 */
void adc_us_il_auto_zero(void);

/**
 * @brief 取最新一组采样，物理量已换算。
 * @return 1 = 拿到新数据；0 = 还没新数据
 */
uint8_t adc_us_il_get_sample(adc_sample_t *out);

/**
 * @brief 取 1024 点连续 ADC 原始数据帧（用于谐波 FFT）
 * @param[out] buf  调用方提供的缓冲，长度 ≥ HARMONIC_FFT_SIZE
 * @param      len  请求长度
 * @return 1 = 帧已就绪并拷贝；0 = 帧还没收满
 */
uint8_t adc_us_il_get_frame(uint16_t *buf, uint32_t len);

#endif /* __ADC_US_IL_H__ */
