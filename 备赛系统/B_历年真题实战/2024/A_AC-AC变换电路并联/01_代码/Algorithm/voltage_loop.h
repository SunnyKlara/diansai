/**
 * @file    voltage_loop.h
 * @brief   输出 Vrms 闭环 PI + 下垂均流叠加
 */

#ifndef __VOLTAGE_LOOP_H__
#define __VOLTAGE_LOOP_H__

#include <stdint.h>

typedef struct {
    float kp;
    float ki;
    float ts;
    float integ;
    float integ_max;
    float integ_min;
    float duty_ff;          /* 开环前馈 = vset / vin                 */
    float droop_r;          /* 下垂电阻                              */
} vloop_t;

void vloop_init(vloop_t *v);
void vloop_set_target(vloop_t *v, float vset_v, float vin_rms);
float vloop_update(vloop_t *v, float vrms_meas, float iout_meas);
void vloop_reset(vloop_t *v);

#endif
