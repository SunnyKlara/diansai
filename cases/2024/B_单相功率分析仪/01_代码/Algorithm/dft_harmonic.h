/**
 * @file    dft_harmonic.h
 * @brief   单频 DFT 提取 1~10 次谐波 + THD 计算
 *
 *  设计：
 *      不做完整 FFT（资源浪费），只算需要的 10 个频率 bin。
 *      运算量 = N × 10 × 2 复数乘加 = 1000 × 20 = 20k 次乘加。
 *      MSP430FR5994 @ 16MHz 软件浮点 ~ 1ms 完成。
 *
 *  原理：
 *      X(k) = Σ x[n] · exp(-j 2π k n / N)
 *      |X(k)| × 2/N = 该频率 bin 的幅值
 *      谐波 RMS = 幅值 / √2
 *      THD = √(I2² + I3² + ... + I10²) / I1 × 100%
 *
 *  优化：
 *      预计算 sin/cos LUT（每个 bin 一份）→ 避免运行时 sinf/cosf。
 *      LUT 在第一次调用时按需构造，后续直接查表。
 */

#ifndef __DFT_HARMONIC_H
#define __DFT_HARMONIC_H

#include <stdint.h>
#include "../config.h"

typedef struct {
    float magnitude[NUM_HARMONICS + 1];  /* [1] = 基波幅值，[2..NUM] = 谐波幅值 */
    float rms[NUM_HARMONICS + 1];        /* 各次谐波 RMS */
    float thd_percent;                    /* THD % */
    float h_norm[NUM_HARMONICS + 1];      /* 归一化（除以基波）*/
} HarmonicResult;

/**
 * @brief 一次性初始化 sin/cos LUT
 *  内部静态分配，调用一次即可。
 */
void DFTHarm_Init(void);

/**
 * @brief 计算谐波 + THD
 * @param i_samples   电流采样（A，已去偏置）
 * @param n           采样点数（必须等于 DFT_N）
 * @param[out] out    结果
 */
void DFTHarm_Compute(const float *i_samples, uint16_t n,
                     HarmonicResult *out);

#endif /* __DFT_HARMONIC_H */
