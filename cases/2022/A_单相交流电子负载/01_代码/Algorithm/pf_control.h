/**
 * @file    pf_control.h
 * @brief   单相功率因数模拟控制（dq 双 PI）
 */
#ifndef __PF_CONTROL_H__
#define __PF_CONTROL_H__

#include <stdint.h>

typedef enum { LOAD_R = 1, LOAD_L = 2, LOAD_C = 3 } load_type_t;

typedef struct {
    load_type_t type;
    float cos_phi;
    float i_amp;
    float id_ref, iq_ref;

    float kp_id, ki_id, integ_id;
    float kp_iq, ki_iq, integ_iq;

    /* 90° 延迟缓冲实现 β 分量 */
    float beta_buf[400];
    uint16_t beta_idx;
} pfc_t;

void pfc_init(pfc_t *pfc);
void pfc_set(pfc_t *pfc, load_type_t type, float cos_phi, float i_amp);
float pfc_update(pfc_t *pfc, float i_meas, float theta);

#endif
