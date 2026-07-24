/*
 * control.c - 通用离散 PID 实现 (条件抗积分饱和 + 微分对测量求导)
 * 待真机整定: 各环 Kp/Ki/Kd 由整定得出后写回调用处(或串口在线调)。
 */
#include "control.h"

void pid_init(pid_t *c, float kp, float ki, float kd, float out_min, float out_max)
{
    c->kp = kp; c->ki = ki; c->kd = kd;
    c->integ = 0.0f;
    c->prev_meas = 0.0f;
    c->out_min = out_min;
    c->out_max = out_max;
    c->started = 0;
}

void pid_reset(pid_t *c)
{
    c->integ = 0.0f;
    c->started = 0;
}

void pid_set_gains(pid_t *c, float kp, float ki, float kd)
{
    c->kp = kp; c->ki = ki; c->kd = kd;
}

float pid_step(pid_t *c, float setpoint, float meas)
{
    float err = setpoint - meas;

    /* 微分项: 对测量求导, 取负 (避免目标突变的微分踢); 首拍不算 */
    float dterm = 0.0f;
    if (c->started) dterm = -c->kd * (meas - c->prev_meas);
    c->prev_meas = meas;
    c->started = 1;

    /* 先试算(含本拍积分), 据是否饱和决定是否真正累加积分 —— 条件抗饱和 */
    float integ_try = c->integ + c->ki * err;
    float out = c->kp * err + integ_try + dterm;

    if (out > c->out_max) {
        out = c->out_max;
        if (err <= 0.0f) c->integ = integ_try;   /* 顶上限: 仅当误差要往下拉才累加 */
    } else if (out < c->out_min) {
        out = c->out_min;
        if (err >= 0.0f) c->integ = integ_try;
    } else {
        c->integ = integ_try;
    }
    return out;
}
