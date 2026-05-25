/**
 * @file    harmonic_fft.c
 * @brief   1024 点 FFT 谐波分析（H₁~H₁₀）+ 汉宁窗 + THD
 *
 * 实现策略：
 *   1. 优先用 CMSIS-DSP arm_cfft_f32（项目已有 STM32F407 + CMSIS-DSP）
 *   2. 离线测试或裸机环境，可定义 HARMONIC_USE_NAIVE_FFT 切到自实现版本
 *
 * Algorithm 层规范：本文件仅 #include 标准库 + config.h；
 * arm_math.h 仅在定义 HARMONIC_USE_CMSIS 时启用。
 */

#include "harmonic_fft.h"
#include "../config.h"
#include <math.h>
#include <string.h>

#ifdef HARMONIC_USE_CMSIS
#include "arm_math.h"
#endif

/* ========================================================================
 * 内部缓冲（静态分配，避免 malloc）
 * ===================================================================== */

static float    s_fft_buf[HARMONIC_FFT_SIZE * 2]; /* 复数：实部/虚部交替    */
static float    s_fft_mag[HARMONIC_FFT_SIZE];     /* 幅度谱                */
static float    s_hann[HARMONIC_FFT_SIZE];        /* 汉宁窗预算表          */

#ifdef HARMONIC_USE_CMSIS
static arm_cfft_instance_f32 s_fft_inst;
#endif

/* ========================================================================
 * 自实现 Cooley-Tukey FFT（无 CMSIS-DSP 时使用）
 * ===================================================================== */

#ifndef HARMONIC_USE_CMSIS

static void naive_cfft_f32(float *data, uint32_t N)
{
    /* 位反转重排 */
    uint32_t j = 0;
    for (uint32_t i = 0; i < N - 1; i++) {
        if (i < j) {
            float tr = data[2*i];
            float ti = data[2*i + 1];
            data[2*i]     = data[2*j];
            data[2*i + 1] = data[2*j + 1];
            data[2*j]     = tr;
            data[2*j + 1] = ti;
        }
        uint32_t k = N >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    /* 蝶形 */
    for (uint32_t s = 1; (1u << s) <= N; s++) {
        uint32_t m  = 1u << s;
        uint32_t mh = m >> 1;
        float    theta = -TWO_PI / (float)m;
        float    wr    = cosf(theta);
        float    wi    = sinf(theta);

        for (uint32_t k = 0; k < N; k += m) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (uint32_t n = 0; n < mh; n++) {
                uint32_t idx_a = (k + n) * 2;
                uint32_t idx_b = (k + n + mh) * 2;

                float t_r = w_r * data[idx_b]     - w_i * data[idx_b + 1];
                float t_i = w_r * data[idx_b + 1] + w_i * data[idx_b];

                data[idx_b]     = data[idx_a]     - t_r;
                data[idx_b + 1] = data[idx_a + 1] - t_i;
                data[idx_a]    += t_r;
                data[idx_a + 1] += t_i;

                /* w *= e^{jθ} */
                float new_wr = w_r * wr - w_i * wi;
                float new_wi = w_r * wi + w_i * wr;
                w_r = new_wr;
                w_i = new_wi;
            }
        }
    }
}

static void naive_cmag_f32(const float *cdata, float *mag, uint32_t N)
{
    for (uint32_t i = 0; i < N; i++) {
        float re = cdata[2*i];
        float im = cdata[2*i + 1];
        mag[i] = sqrtf(re * re + im * im);
    }
}

#endif /* !HARMONIC_USE_CMSIS */

/* ========================================================================
 * 公开 API
 * ===================================================================== */

void harmonic_init(void)
{
#ifdef HARMONIC_USE_CMSIS
    arm_cfft_init_f32(&s_fft_inst, HARMONIC_FFT_SIZE);
#endif

    /* 预算汉宁窗：w[k] = 0.5·(1 - cos(2πk / (N-1))) */
    const uint32_t N = HARMONIC_FFT_SIZE;
    for (uint32_t i = 0; i < N; i++) {
        s_hann[i] = 0.5f * (1.0f - cosf(TWO_PI * (float)i / (float)(N - 1)));
    }
}

void harmonic_analyze(const uint16_t *adc_data, float scale, float offset,
                      harmonic_result_t *r)
{
    if ((adc_data == 0) || (r == 0)) {
        return;
    }

    const uint32_t N = HARMONIC_FFT_SIZE;

    /* ===== 1. 时域转换 + 时域 RMS 计算（与窗无关，作为校验） ===== */
    float sum_sq = 0.0f;
    for (uint32_t i = 0; i < N; i++) {
        float v = ((float)adc_data[i] - offset) * scale;
        s_fft_buf[2*i]     = v;
        s_fft_buf[2*i + 1] = 0.0f;
        sum_sq += v * v;
    }
    r->i_rms = sqrtf(sum_sq / (float)N);

    /* ===== 2. 加汉宁窗 ===== */
    for (uint32_t i = 0; i < N; i++) {
        s_fft_buf[2*i] *= s_hann[i];
    }

    /* ===== 3. FFT ===== */
#ifdef HARMONIC_USE_CMSIS
    arm_cfft_f32(&s_fft_inst, s_fft_buf, 0, 1);
    arm_cmplx_mag_f32(s_fft_buf, s_fft_mag, N);
#else
    naive_cfft_f32(s_fft_buf, N);
    naive_cmag_f32(s_fft_buf, s_fft_mag, N);
#endif

    /* ===== 4. 提取各次谐波 RMS =====
     *   归一化 = 2/N（双边谱单边化）× 汉宁窗能量补偿（×2）
     *   再 ÷ √2 由峰值转 RMS
     */
    const float norm = (2.0f / (float)N) * HANN_WINDOW_GAIN;

    memset(r->harmonic_rms, 0, sizeof(r->harmonic_rms));
    memset(r->Hi_pct, 0, sizeof(r->Hi_pct));

    for (uint32_t h = 0; h <= HARMONIC_MAX_ORDER; h++) {
        uint32_t idx = FUND_BIN_INDEX * h;
        if (idx >= (N >> 1)) {
            break;
        }

        /* 取目标 bin 与左右各 1 bin 的最大值，抗频谱泄漏 */
        float peak = s_fft_mag[idx];
        if ((idx > 0) && (s_fft_mag[idx - 1] > peak)) {
            peak = s_fft_mag[idx - 1];
        }
        if ((idx + 1 < (N >> 1)) && (s_fft_mag[idx + 1] > peak)) {
            peak = s_fft_mag[idx + 1];
        }

        /* 峰值 → RMS */
        r->harmonic_rms[h] = peak * norm * INV_SQRT2;
    }

    r->i_fund_rms = r->harmonic_rms[1];

    /* ===== 5. Hi% & THD ===== */
    if (r->i_fund_rms > THD_MIN_FUND_A) {
        float sum_h_sq = 0.0f;
        for (uint32_t h = 2; h <= HARMONIC_MAX_ORDER; h++) {
            r->Hi_pct[h] = r->harmonic_rms[h] / r->i_fund_rms * 100.0f;
            sum_h_sq    += r->harmonic_rms[h] * r->harmonic_rms[h];
        }
        r->thd_pct = sqrtf(sum_h_sq) / r->i_fund_rms * 100.0f;
        r->valid   = 1;
    } else {
        r->thd_pct = 0.0f;
        r->valid   = 0;
    }
}
