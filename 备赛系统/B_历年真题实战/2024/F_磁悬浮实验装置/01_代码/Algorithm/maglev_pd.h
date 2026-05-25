/**
 * @file    maglev_pd.h
 * @brief   磁悬浮 PD 控制器（微分先行）
 */
#ifndef __MAGLEV_PD_H__
#define __MAGLEV_PD_H__
#include <stdint.h>

typedef struct {
    float kp, kd, ki;
    float integ;
    float prev_meas;
    float out_min, out_max;
} maglev_pd_t;

void maglev_pd_init(maglev_pd_t *p, float kp, float kd, float ki);
float maglev_pd_update(maglev_pd_t *p, float setpoint_mm, float meas_mm, float dt);
void maglev_pd_reset(maglev_pd_t *p);

#endif
