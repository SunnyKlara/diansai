/**
 * @file    ac_chopper.c
 * @brief   AC-AC 斩波 + 四步换流算法
 */

#include "ac_chopper.h"
#include "../config.h"

static float clamp_duty(float d)
{
    if (d < DUTY_MIN) return DUTY_MIN;
    if (d > DUTY_MAX) return DUTY_MAX;
    return d;
}

void chop_compute_pwm(chop_half_t half, float duty, chop_pwm_t *out)
{
    if (out == 0) return;
    float d = clamp_duty(duty);

    if (half == CHOP_HALF_POSITIVE) {
        /* S1 组斩波，S2 组关断 */
        out->ccr1 = d;
        out->ccr2 = d;
        out->ccr3 = 0.0f;
        out->ccr4 = 0.0f;
    } else {
        out->ccr1 = 0.0f;
        out->ccr2 = 0.0f;
        out->ccr3 = d;
        out->ccr4 = d;
    }
}

/**
 * 四步换流（正→负为例，电流方向 io > 0）：
 *   step 0: 关 S1 中不通流的管（CCR2=0）
 *   step 1: 开 S2 中将通流的管（CCR3 = d）
 *   step 2: 关 S1 中通流的管（CCR1 = 0）
 *   step 3: 开 S2 另一个管（CCR4 = d）
 */
void chop_step_commutation(uint8_t step, chop_half_t next,
                           float duty, chop_pwm_t *out)
{
    if (out == 0) return;
    float d = clamp_duty(duty);

    if (next == CHOP_HALF_NEGATIVE) {
        /* 正→负 */
        switch (step & 3) {
            case 0: out->ccr1 = d;   out->ccr2 = 0;   out->ccr3 = 0;   out->ccr4 = 0; break;
            case 1: out->ccr1 = d;   out->ccr2 = 0;   out->ccr3 = d;   out->ccr4 = 0; break;
            case 2: out->ccr1 = 0;   out->ccr2 = 0;   out->ccr3 = d;   out->ccr4 = 0; break;
            case 3: out->ccr1 = 0;   out->ccr2 = 0;   out->ccr3 = d;   out->ccr4 = d; break;
        }
    } else {
        /* 负→正 */
        switch (step & 3) {
            case 0: out->ccr1 = 0;   out->ccr2 = 0;   out->ccr3 = d;   out->ccr4 = 0; break;
            case 1: out->ccr1 = d;   out->ccr2 = 0;   out->ccr3 = d;   out->ccr4 = 0; break;
            case 2: out->ccr1 = d;   out->ccr2 = 0;   out->ccr3 = 0;   out->ccr4 = 0; break;
            case 3: out->ccr1 = d;   out->ccr2 = d;   out->ccr3 = 0;   out->ccr4 = 0; break;
        }
    }
}
