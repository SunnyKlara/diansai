/**
 * @file    power_calc.c
 * @brief   时域功率计算实现
 */

#include "power_calc.h"
#include <math.h>

void PowerCalc_TimeDomain(const float *v_samples,
                          const float *i_samples,
                          uint16_t n,
                          TimeDomainResult *out)
{
    if (!v_samples || !i_samples || !out || n == 0u) {
        if (out) {
            out->v_rms = 0.0f;
            out->i_rms = 0.0f;
            out->p_active = 0.0f;
            out->s_apparent = 0.0f;
            out->pf = 0.0f;
        }
        return;
    }

    /* 用 double 累积避免大数据集浮点误差 */
    double sum_v2 = 0.0;
    double sum_i2 = 0.0;
    double sum_vi = 0.0;

    for (uint16_t k = 0; k < n; k++) {
        double v = (double)v_samples[k];
        double i = (double)i_samples[k];
        sum_v2 += v * v;
        sum_i2 += i * i;
        sum_vi += v * i;
    }

    double inv_n = 1.0 / (double)n;

    out->v_rms      = (float)sqrt(sum_v2 * inv_n);
    out->i_rms      = (float)sqrt(sum_i2 * inv_n);
    out->p_active   = (float)(sum_vi * inv_n);
    out->s_apparent = out->v_rms * out->i_rms;

    /* PF = P / S，注意 S 接近 0 时返回 0 避免 NaN */
    if (out->s_apparent > 0.001f) {
        out->pf = out->p_active / out->s_apparent;
        /* 限幅到 [-1, 1] 避免数值误差导致 |PF| > 1 */
        if (out->pf >  1.0f) out->pf =  1.0f;
        if (out->pf < -1.0f) out->pf = -1.0f;
    } else {
        out->pf = 0.0f;
    }
}
