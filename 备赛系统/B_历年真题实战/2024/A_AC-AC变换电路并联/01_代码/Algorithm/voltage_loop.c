/**
 * @file    voltage_loop.c
 * @brief   输出 Vrms PI + 下垂控制
 */

#include "voltage_loop.h"
#include "../config.h"

void vloop_init(vloop_t *v)
{
    if (v == 0) return;
    v->kp        = KP_VOUT;
    v->ki        = KI_VOUT;
    v->ts        = 1.0f / (float)VRMS_LOOP_HZ;
    v->integ     = 0.0f;
    v->integ_max = VOUT_INTEG_MAX;
    v->integ_min = VOUT_INTEG_MIN;
    v->duty_ff   = 0.0f;
    v->droop_r   = DROOP_R_OHM;
}

void vloop_set_target(vloop_t *v, float vset_v, float vin_rms)
{
    if (v == 0) return;
    if (vin_rms < 1.0f) vin_rms = 1.0f;
    v->duty_ff = vset_v / vin_rms;
    v->integ   = v->duty_ff;
}

void vloop_reset(vloop_t *v)
{
    if (v == 0) return;
    v->integ = v->duty_ff;
}

float vloop_update(vloop_t *v, float vrms_meas, float iout_meas)
{
    if (v == 0) return DUTY_MIN;

    /* 下垂修正：Vset_eff = Vset - Rd · I */
    float vset_eff_correction = v->droop_r * iout_meas;
    float err = -vset_eff_correction + (v->duty_ff - vrms_meas) * 0.0f; /* 占位 */

    /* 注意：err 应基于 (Vset - Rd*I) - Vmeas 转换占空比域。
     * 简化：用 PI 直接收敛 vrms 到 (vset - Rd*I)。
     */
    float vset_target = (v->duty_ff /* duty_ff = vset/vin */) * 1.0f - vset_eff_correction;
    err = vset_target - (vrms_meas / 36.0f); /* 归一到 0..1 */

    v->integ += v->ki * err * v->ts * 100.0f;
    if (v->integ > v->integ_max) v->integ = v->integ_max;
    if (v->integ < v->integ_min) v->integ = v->integ_min;

    float duty = v->kp * err + v->integ;
    if (duty < DUTY_MIN) duty = DUTY_MIN;
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    return duty;
}
