/**
 * @file    buck_loop.c
 */
#include "buck_loop.h"
#include "../config.h"

static float clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void buck_init(buck_t *b)
{
    if (!b) return;
    b->kp_v = BUCK_KP_VOUT;  b->ki_v = BUCK_KI_VOUT; b->ts_v = 1.0f / (float)VOUT_LOOP_HZ; b->integ_v = 0.0f;
    b->kp_i = BUCK_KP_IL;    b->ki_i = BUCK_KI_IL;   b->ts_i = 1.0f / (float)BUCK_FSW_HZ;  b->integ_i = 0.0f;
    b->vout_ref = VOUT_NOMINAL_V;
    b->duty_min = BUCK_DUTY_MIN;
    b->duty_max = BUCK_DUTY_MAX;
}

void buck_set_vout(buck_t *b, float v) { if (b) b->vout_ref = v; }

float buck_update(buck_t *b, float vout_meas, float il_meas)
{
    if (!b) return BUCK_DUTY_MIN;

    /* 电压外环 → IL_ref */
    float v_err = b->vout_ref - vout_meas;
    b->integ_v = clamp(b->integ_v + b->ki_v * v_err * b->ts_v, -2.5f, 2.5f);
    float il_ref = clamp(b->kp_v * v_err + b->integ_v, 0.0f, 4.0f);

    /* 电流内环 → 占空比 */
    float i_err = il_ref - il_meas;
    b->integ_i = clamp(b->integ_i + b->ki_i * i_err * b->ts_i,
                       -0.4f, 0.4f);
    float duty = b->kp_i * i_err + b->integ_i;
    return clamp(duty, b->duty_min, b->duty_max);
}
