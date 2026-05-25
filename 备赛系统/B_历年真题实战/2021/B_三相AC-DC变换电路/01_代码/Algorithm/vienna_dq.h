/**
 * @file    vienna_dq.h
 * @brief   Vienna 整流器 DQ 双闭环（电流内环 + 母线电压外环）
 */
#ifndef __VIENNA_DQ_H__
#define __VIENNA_DQ_H__

#include <stdint.h>

typedef struct {
    float kp; float ki; float ts; float integ; float integ_max;
} pi_t;

typedef struct {
    pi_t  loop_id;            /* d 轴电流环              */
    pi_t  loop_iq;            /* q 轴电流环              */
    pi_t  loop_vbus;          /* 母线电压环              */
    float vbus_ref;           /* 母线设定                */
    float L;                  /* 网侧电感（解耦用）      */
} vienna_t;

void vienna_init(vienna_t *v);
void vienna_set_vbus(vienna_t *v, float vbus_ref);

/**
 * @brief Vienna 双环更新
 * @param[in] vbus_meas      母线电压
 * @param[in] id, iq         dq 电流测量
 * @param[in] vd, vq         dq 电压测量
 * @param[in] omega          电角速度（PLL）
 * @param[out] mod_d, mod_q  输出调制比
 */
void vienna_update(vienna_t *v, float vbus_meas, float id, float iq,
                   float vd, float vq, float omega,
                   float *mod_d, float *mod_q);

#endif
