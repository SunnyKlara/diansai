/**
 * @file    apf_compensator.c
 * @brief   APF 基波提取（滑动 DFT）+ 电流跟踪 PI
 *
 * 算法层：仅依赖 stdint.h / math.h / config.h。
 * 不得 #include "stm32xxx_hal.h" 或 driver 头。
 */

#include "apf_compensator.h"
#include "../config.h"
#include <math.h>

/* ========================================================================
 * 1. 基波提取（滑动 DFT）
 * ===================================================================== */

void apf_extractor_init(apf_extractor_t *ext)
{
    if (ext == 0) {
        return;
    }

    /* 预算 sin/cos 表：i = 0..N-1 → 角度 2πi/N */
    for (uint16_t i = 0; i < APF_CYCLE_SAMPLES; i++) {
        float angle = TWO_PI * (float)i / (float)APF_CYCLE_SAMPLES;
        ext->sin_table[i] = sinf(angle);
        ext->cos_table[i] = cosf(angle);
        ext->il_buffer[i] = 0.0f;
    }

    ext->buf_idx = 0;
    ext->a1 = 0.0f;
    ext->b1 = 0.0f;
    ext->inited = 1;
}

/**
 * 滑动 DFT 单点更新（O(1) 递推版本）
 *
 * 标准 DFT：a1 = (2/N) Σ x[k]·sin(2πk/N)
 * 滑动递推：当新点 x_new 进、旧点 x_old 出时
 *           a1_new = a1_old + (2/N)·(x_new - x_old)·sin(2πk_new/N)
 *
 * 注意：本实现做了简化——每次直接用 buf_idx 处的 sin/cos 索引。
 * 这要求 buf_idx 单调推进 N 次后归零，即环形缓冲。
 */
float apf_extractor_update(apf_extractor_t *ext, float il_now)
{
    if ((ext == 0) || (ext->inited == 0)) {
        return 0.0f;
    }

    const uint16_t N = (uint16_t)APF_CYCLE_SAMPLES;
    const float    inv_half_N = 2.0f / (float)N;

    /* 取出"被替换"的旧样本，做 O(1) 递推 */
    float il_old = ext->il_buffer[ext->buf_idx];
    float sin_k  = ext->sin_table[ext->buf_idx];
    float cos_k  = ext->cos_table[ext->buf_idx];

    ext->a1 += inv_half_N * (il_now - il_old) * sin_k;
    ext->b1 += inv_half_N * (il_now - il_old) * cos_k;

    /* 写新样本到环形缓冲 */
    ext->il_buffer[ext->buf_idx] = il_now;

    /* 重构当前时刻基波瞬时值：iL_fund(t) = a1·sin(ωt) + b1·cos(ωt) */
    float il_fund = ext->a1 * sin_k + ext->b1 * cos_k;

    /* 推进索引（环形） */
    ext->buf_idx++;
    if (ext->buf_idx >= N) {
        ext->buf_idx = 0;
    }

    /* 谐波分量 = 总量 - 基波 = APF 应输出的补偿电流参考 */
    return il_now - il_fund;
}

#ifdef APF_UNIT_TEST
void apf_extractor_get_dft(const apf_extractor_t *ext, float *a1, float *b1)
{
    if ((ext == 0) || (a1 == 0) || (b1 == 0)) {
        return;
    }
    *a1 = ext->a1;
    *b1 = ext->b1;
}
#endif

/* ========================================================================
 * 2. 电流跟踪 PI
 * ===================================================================== */

void apf_pi_init(apf_pi_t *pi)
{
    if (pi == 0) {
        return;
    }
    pi->kp           = APF_KP;
    pi->ki           = APF_KI;
    pi->ts           = APF_CTRL_PERIOD_S;
    pi->integ        = 0.0f;
    pi->integ_max    = APF_INTEG_MAX;
    pi->duty_center  = APF_DUTY_CENTER;
    pi->duty_min     = APF_DUTY_MIN;
    pi->duty_max     = APF_DUTY_MAX;
    pi->ramp         = 0.0f;
    pi->ramp_step    = APF_SOFT_START_STEP;
}

void apf_pi_soft_start(apf_pi_t *pi)
{
    if (pi == 0) {
        return;
    }
    pi->integ = 0.0f;
    pi->ramp  = 0.0f;
}

void apf_pi_disable(apf_pi_t *pi)
{
    if (pi == 0) {
        return;
    }
    pi->integ = 0.0f;
    pi->ramp  = 0.0f;
}

float apf_pi_update(apf_pi_t *pi, float if_ref, float if_act)
{
    if (pi == 0) {
        return 0.5f;
    }

    /* 软启动 ramp 推进 */
    if (pi->ramp < 1.0f) {
        pi->ramp += pi->ramp_step;
        if (pi->ramp > 1.0f) {
            pi->ramp = 1.0f;
        }
    }

    /* 误差 */
    float err = (if_ref * pi->ramp) - if_act;

    /* 积分（带限幅） */
    pi->integ += pi->ki * err * pi->ts;
    if (pi->integ >  pi->integ_max) pi->integ =  pi->integ_max;
    if (pi->integ < -pi->integ_max) pi->integ = -pi->integ_max;

    /* PI 输出 + 中点偏置 */
    float duty = pi->duty_center + pi->kp * err + pi->integ;

    /* 占空比限幅 */
    if (duty > pi->duty_max) duty = pi->duty_max;
    if (duty < pi->duty_min) duty = pi->duty_min;

    return duty;
}
