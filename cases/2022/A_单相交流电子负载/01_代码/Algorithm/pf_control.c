/**
 * @file    pf_control.c
 */
#include "pf_control.h"
#include "../config.h"
#include <math.h>

static float clamp(float x, float lo, float hi)
{
    if (x < lo) return lo; if (x > hi) return hi; return x;
}

void pfc_init(pfc_t *p)
{
    if (!p) return;
    p->type = LOAD_R; p->cos_phi = 1.0f; p->i_amp = I_AMP_DEFAULT_A;
    p->id_ref = p->i_amp; p->iq_ref = 0.0f;
    p->kp_id = KP_ID; p->ki_id = KI_ID; p->integ_id = 0.0f;
    p->kp_iq = KP_IQ; p->ki_iq = KI_IQ; p->integ_iq = 0.0f;
    for (uint16_t i = 0; i < QUARTER_DELAY * 4; i++) p->beta_buf[i] = 0.0f;
    p->beta_idx = 0;
}

void pfc_set(pfc_t *p, load_type_t type, float cos_phi, float i_amp)
{
    if (!p) return;
    if (cos_phi < COSPHI_MIN) cos_phi = COSPHI_MIN;
    if (cos_phi > COSPHI_MAX) cos_phi = COSPHI_MAX;

    p->type    = type;
    p->cos_phi = cos_phi;
    p->i_amp   = i_amp;

    float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
    p->id_ref = i_amp * cos_phi;
    switch (type) {
        case LOAD_R: p->iq_ref =  0.0f; break;
        case LOAD_L: p->iq_ref = -i_amp * sin_phi; break; /* 滞后 */
        case LOAD_C: p->iq_ref =  i_amp * sin_phi; break; /* 超前 */
        default:     p->iq_ref =  0.0f; break;
    }
}

float pfc_update(pfc_t *p, float i_meas, float theta)
{
    if (!p) return 0.5f;

    /* 1. 用延迟 N/4 点制造 β 分量 */
    float i_alpha = i_meas;
    float i_beta  = p->beta_buf[p->beta_idx];
    p->beta_buf[p->beta_idx] = i_meas;
    p->beta_idx++;
    if (p->beta_idx >= QUARTER_DELAY) p->beta_idx = 0;

    /* 2. Park */
    float c = cosf(theta), s = sinf(theta);
    float id =  i_alpha * c + i_beta * s;
    float iq = -i_alpha * s + i_beta * c;

    /* 3. dq PI */
    float err_d = p->id_ref - id;
    float err_q = p->iq_ref - iq;
    p->integ_id = clamp(p->integ_id + p->ki_id * err_d * CTRL_PERIOD_S,
                        -ID_INTEG_MAX, ID_INTEG_MAX);
    p->integ_iq = clamp(p->integ_iq + p->ki_iq * err_q * CTRL_PERIOD_S,
                        -IQ_INTEG_MAX, IQ_INTEG_MAX);
    float vd = p->kp_id * err_d + p->integ_id;
    float vq = p->kp_iq * err_q + p->integ_iq;

    /* 4. 逆 Park → α 分量（单相 SPWM 只需 α） */
    float v_alpha = vd * c - vq * s;

    /* 5. 转占空比 */
    float duty = clamp(0.5f + v_alpha * 0.5f, DUTY_MIN, DUTY_MAX);
    return duty;
}
