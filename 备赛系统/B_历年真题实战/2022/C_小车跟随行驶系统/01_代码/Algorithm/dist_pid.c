/**
 * @file    dist_pid.c
 * @brief   间距 PID（跟随车控制车速维持 20cm 间距）
 */
#include "dist_pid.h"
#include "../config.h"

void dist_pid_init(dist_pid_t *p)
{
    if (!p) return;
    p->kp = DIST_KP; p->ki = DIST_KI; p->kd = DIST_KD;
    p->prev_err = 0; p->integ = 0; p->v_base_mps = V_LEAD_SLOW_MPS;
}

void dist_pid_set_base_speed(dist_pid_t *p, float v_base)
{
    if (p) p->v_base_mps = v_base;
}

float dist_pid_update(dist_pid_t *p, float dist_cm)
{
    if (!p) return 0.0f;
    float err = dist_cm - DIST_TARGET_CM;
    float de  = err - p->prev_err;
    p->prev_err = err;
    p->integ += err * CTRL_PERIOD_S;
    if (p->integ >  20.0f) p->integ =  20.0f;
    if (p->integ < -20.0f) p->integ = -20.0f;
    float dv = p->kp * err + p->ki * p->integ + p->kd * de;
    /* 输出 = 基础速度 + 调节量（mm/s 转 m/s） */
    float v = p->v_base_mps + dv * 0.001f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.5f) v = 1.5f;
    return v;
}
