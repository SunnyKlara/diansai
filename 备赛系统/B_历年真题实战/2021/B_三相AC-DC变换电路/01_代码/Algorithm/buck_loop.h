/**
 * @file    buck_loop.h
 * @brief   Buck 后级稳压双环（电压外环 + 电流内环）
 */
#ifndef __BUCK_LOOP_H__
#define __BUCK_LOOP_H__

#include <stdint.h>

typedef struct {
    float kp_v; float ki_v; float ts_v; float integ_v;
    float kp_i; float ki_i; float ts_i; float integ_i;
    float vout_ref;
    float duty_min;
    float duty_max;
} buck_t;

void buck_init(buck_t *b);
void buck_set_vout(buck_t *b, float vout_ref);
float buck_update(buck_t *b, float vout_meas, float il_meas);

#endif
