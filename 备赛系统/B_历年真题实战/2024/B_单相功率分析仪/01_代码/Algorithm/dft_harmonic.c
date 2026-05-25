/**
 * @file    dft_harmonic.c
 * @brief   单频 DFT × 10 谐波 + THD 实现
 */

#include "dft_harmonic.h"
#include <math.h>

#define TWO_PI  6.28318530718f

/* sin/cos LUT：每个谐波 bin 一份完整周期的 sin/cos
 *  尺寸：NUM_HARMONICS × DFT_N × 2(sin + cos) × 4 字节
 *      = 10 × 1000 × 8 = 80 KB → MSP430 SRAM 不够
 *  对策：只存"基波"一份 LUT，谐波通过下标乘法在线索引。
 *      sin(2π · h · n / N) = sin( 2π · (h × n) mod N / N )
 *      用基波 LUT 直接查表，节省 90% 内存。
 */
static float s_sin_lut[DFT_N];
static float s_cos_lut[DFT_N];
static uint8_t s_lut_ready = 0;

void DFTHarm_Init(void)
{
    if (s_lut_ready) return;
    for (uint16_t k = 0; k < DFT_N; k++) {
        float angle = TWO_PI * (float)k / (float)DFT_N;
        s_sin_lut[k] = sinf(angle);
        s_cos_lut[k] = cosf(angle);
    }
    s_lut_ready = 1u;
}

/* 单频 DFT：计算 X(bin) 的幅值 */
static float dft_bin_magnitude(const float *x, uint16_t n, uint16_t bin)
{
    /* 用 double 累积防止大数据集精度损失 */
    double re = 0.0;
    double im = 0.0;

    for (uint16_t k = 0; k < n; k++) {
        /* (bin × k) mod N → LUT 索引 */
        uint32_t lut_idx = ((uint32_t)bin * (uint32_t)k) % (uint32_t)n;
        re += (double)x[k] * (double)s_cos_lut[lut_idx];
        im -= (double)x[k] * (double)s_sin_lut[lut_idx];
    }

    /* 单边幅值：|X| × 2/N */
    double mag = sqrt(re * re + im * im) * 2.0 / (double)n;
    return (float)mag;
}

void DFTHarm_Compute(const float *i_samples, uint16_t n, HarmonicResult *out)
{
    if (!i_samples || !out || n != DFT_N) {
        if (out) {
            for (uint8_t h = 0; h <= NUM_HARMONICS; h++) {
                out->magnitude[h] = 0.0f;
                out->rms[h] = 0.0f;
                out->h_norm[h] = 0.0f;
            }
            out->thd_percent = 0.0f;
        }
        return;
    }

    if (!s_lut_ready) DFTHarm_Init();

    /* ---- 提取各次谐波 ---- */
    for (uint8_t h = 1; h <= NUM_HARMONICS; h++) {
        uint16_t bin = (uint16_t)FUND_BIN * (uint16_t)h;
        if (bin >= (uint16_t)(DFT_N / 2u)) {
            out->magnitude[h] = 0.0f;
            out->rms[h] = 0.0f;
            continue;
        }
        out->magnitude[h] = dft_bin_magnitude(i_samples, n, bin);
        out->rms[h] = out->magnitude[h] * 0.7071068f;   /* 峰值 → RMS */
    }

    /* ---- 归一化 + THD ---- */
    out->h_norm[1] = 1.0f;
    if (out->rms[1] > 1e-6f) {
        double sum_harm_sq = 0.0;
        for (uint8_t h = 2; h <= NUM_HARMONICS; h++) {
            out->h_norm[h] = out->rms[h] / out->rms[1];
            sum_harm_sq += (double)out->rms[h] * (double)out->rms[h];
        }
        out->thd_percent = (float)(sqrt(sum_harm_sq) / (double)out->rms[1] * 100.0);
    } else {
        for (uint8_t h = 2; h <= NUM_HARMONICS; h++) out->h_norm[h] = 0.0f;
        out->thd_percent = 0.0f;
    }
}
