/**
 * @file    harmonic_fft.h
 * @brief   1024 点 FFT 谐波分析（H₁~H₁₀）+ 汉宁窗 + THD
 * @note    Algorithm 层：仅依赖 stdint/math，不依赖驱动 / HAL。
 *          FFT 引擎在 .c 内通过 typedef 抽象，方便替换 CMSIS-DSP 或自实现。
 */

#ifndef __HARMONIC_FFT_H__
#define __HARMONIC_FFT_H__

#include <stdint.h>

/**
 * @brief 谐波分析结果（基本要求 (3) 输出格式）
 */
typedef struct {
    float i_rms;                    /* 时域 RMS（A）                    */
    float i_fund_rms;               /* 基波 RMS（A）                    */
    float harmonic_rms[11];         /* H₀~H₁₀ 各次谐波 RMS（A）        */
    float Hi_pct[11];               /* 单次谐波含有率（% of H₁）       */
    float thd_pct;                  /* 总谐波失真（%）                  */
    uint8_t valid;                  /* 1 = 计算成功；0 = 基波过小      */
} harmonic_result_t;

/**
 * @brief 模块初始化：分配 FFT 实例 + 预算汉宁窗。
 *        必须在使用前调用一次。
 */
void harmonic_init(void);

/**
 * @brief 对一段 ADC 原始数据执行 FFT + 汉宁窗 + 谐波分量提取。
 *
 * @param[in]  adc_data  ADC 原始 12 bit 数据，长度 = HARMONIC_FFT_SIZE
 * @param[in]  scale     LSB → 物理量换算系数（A/LSB 或 V/LSB）
 * @param[in]  offset    ADC 零点偏置（LSB）
 * @param[out] r         结果输出（不能为空）
 *
 * @note 时序：1024 点 @ 10 kHz = 102.4 ms 才能凑齐一帧；运算耗时 ~3 ms @ 168MHz。
 */
void harmonic_analyze(const uint16_t *adc_data, float scale, float offset,
                      harmonic_result_t *r);

#endif /* __HARMONIC_FFT_H__ */
