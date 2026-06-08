/**
 * @file    modulation_detect.c
 * @brief   AM/FM/2ASK/2FSK/2PSK 5 种调制方式识别 + 参数提取
 *
 * 思路：基于包络与瞬时频率的统计特征
 *   - σ_a (归一化包络方差)：区分恒包络（FM/2FSK/2PSK）和非恒包络（AM/2ASK）
 *   - σ_f (瞬时频率方差)：区分恒频（AM/2ASK/2PSK）和变频（FM/2FSK）
 *   - σ_phi (瞬时相位方差)：区分相位连续 vs 相位跳变
 */
#include "modulation_detect.h"
#include "../config.h"
#include <math.h>

static float clamp_f(float x, float lo, float hi){ if(x<lo)return lo;if(x>hi)return hi;return x; }

void mod_init(void) {}

void mod_analyze(const float *iq_i, const float *iq_q,
                 uint32_t len, mod_result_t *out)
{
    if (!iq_i || !iq_q || !out || len < 64) return;

    /* 1. 包络 a[n] = sqrt(I² + Q²) */
    float a_mean = 0;
    static float a[MOD_FRAME_LEN];
    for (uint32_t i = 0; i < len; i++) {
        a[i] = sqrtf(iq_i[i] * iq_i[i] + iq_q[i] * iq_q[i]);
        a_mean += a[i];
    }
    a_mean /= (float)len;
    float a_var = 0;
    for (uint32_t i = 0; i < len; i++) {
        float d = a[i] / a_mean - 1.0f;
        a_var += d * d;
    }
    out->sigma_a_norm = sqrtf(a_var / (float)len);

    /* 2. 瞬时相位 φ[n] = atan2(Q, I) → unwrap → 瞬时频率 = dφ/dt */
    static float phi[MOD_FRAME_LEN];
    phi[0] = atan2f(iq_q[0], iq_i[0]);
    for (uint32_t i = 1; i < len; i++) {
        phi[i] = atan2f(iq_q[i], iq_i[i]);
        float d = phi[i] - phi[i - 1];
        while (d > PI)  d -= TWO_PI;
        while (d < -PI) d += TWO_PI;
        phi[i] = phi[i - 1] + d;
    }

    float f_mean = 0;
    for (uint32_t i = 1; i < len; i++) f_mean += phi[i] - phi[i - 1];
    f_mean /= (float)(len - 1);
    float f_var = 0;
    for (uint32_t i = 1; i < len; i++) {
        float d = (phi[i] - phi[i - 1]) - f_mean;
        f_var += d * d;
    }
    out->sigma_f_norm = sqrtf(f_var / (float)(len - 1));

    /* 3. 相位变化的统计 */
    float phi_var = 0;
    float phi_mean = phi[len / 2];
    for (uint32_t i = 0; i < len; i++) {
        float d = phi[i] - phi_mean;
        phi_var += d * d;
    }
    out->sigma_phi = sqrtf(phi_var / (float)len);

    /* 4. 决策树（阈值实测调）*/
    if (out->sigma_a_norm > 0.30f) {
        if (out->sigma_a_norm > 0.60f) out->type = MOD_2ASK;
        else                            out->type = MOD_AM;
    } else {
        /* 恒包络 */
        if (out->sigma_f_norm > 0.20f) {
            out->type = MOD_FM;
            if (out->sigma_f_norm > 0.50f) out->type = MOD_2FSK;
        } else {
            out->type = MOD_2PSK;
        }
    }

    out->confidence = clamp_f(1.0f - out->sigma_a_norm * out->sigma_f_norm * 0.5f,
                              0.0f, 1.0f);
}
