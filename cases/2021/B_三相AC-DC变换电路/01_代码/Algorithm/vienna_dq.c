/**
 * @file    vienna_dq.c
 */
#include "vienna_dq.h"
#include "../config.h"

static float pi_step(pi_t *pi, float err)
{
    pi->integ += pi->ki * err * pi->ts;
    if (pi->integ >  pi->integ_max) pi->integ =  pi->integ_max;
    if (pi->integ < -pi->integ_max) pi->integ = -pi->integ_max;
    return pi->kp * err + pi->integ;
}

void vienna_init(vienna_t *v)
{
    if (!v) return;
    v->loop_id.kp = VIENNA_KP_ID;
    v->loop_id.ki = VIENNA_KI_ID;
    v->loop_id.ts = 1.0f / (float)CTRL_FREQ_HZ;
    v->loop_id.integ = 0.0f;
    v->loop_id.integ_max = 0.5f;

    v->loop_iq.kp = VIENNA_KP_IQ;
    v->loop_iq.ki = VIENNA_KI_IQ;
    v->loop_iq.ts = v->loop_id.ts;
    v->loop_iq.integ = 0.0f;
    v->loop_iq.integ_max = 0.5f;

    v->loop_vbus.kp = VIENNA_KP_VBUS;
    v->loop_vbus.ki = VIENNA_KI_VBUS;
    v->loop_vbus.ts = 1.0f / (float)VBUS_LOOP_HZ;
    v->loop_vbus.integ = 0.0f;
    v->loop_vbus.integ_max = 5.0f;

    v->vbus_ref = VBUS_NOMINAL_V;
    v->L        = 1.0e-3f;
}

void vienna_set_vbus(vienna_t *v, float vbus_ref) { if (v) v->vbus_ref = vbus_ref; }

void vienna_update(vienna_t *v, float vbus_meas, float id, float iq,
                   float vd, float vq, float omega,
                   float *mod_d, float *mod_q)
{
    if (!v) return;

    /* 1. 母线电压外环 → id_ref */
    float v_err  = v->vbus_ref - vbus_meas;
    float id_ref = pi_step(&v->loop_vbus, v_err);

    /* 2. iq_ref = 0（单位功率因数） */
    float iq_ref = 0.0f;

    /* 3. 电流内环 + 解耦 */
    float ud = pi_step(&v->loop_id, id_ref - id) + vd - omega * v->L * iq;
    float uq = pi_step(&v->loop_iq, iq_ref - iq) + vq + omega * v->L * id;

    /* 4. 调制比归一化 */
    float ud_n = ud / (vbus_meas * 0.5f + 1e-6f);
    float uq_n = uq / (vbus_meas * 0.5f + 1e-6f);

    /* 5. 限幅 */
    float mag = ud_n * ud_n + uq_n * uq_n;
    if (mag > VIENNA_M_MAX * VIENNA_M_MAX) {
        float scale = VIENNA_M_MAX / __builtin_sqrtf(mag);
        ud_n *= scale; uq_n *= scale;
    }

    if (mod_d) *mod_d = ud_n;
    if (mod_q) *mod_q = uq_n;
}
