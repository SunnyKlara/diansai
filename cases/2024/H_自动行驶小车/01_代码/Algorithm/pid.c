/**
 * @file    pid.c
 * @brief   通用 PID 实现
 */

#include "pid.h"

void PID_Init(PID_Controller *pid, float kp, float ki, float kd,
              float out_min, float out_max)
{
    pid->Kp           = kp;
    pid->Ki           = ki;
    pid->Kd           = kd;
    pid->setpoint     = 0.0f;
    pid->error        = 0.0f;
    pid->last_error   = 0.0f;
    pid->prev_error   = 0.0f;
    pid->integral     = 0.0f;
    pid->output       = 0.0f;
    pid->output_min   = out_min;
    pid->output_max   = out_max;
    /* 积分限幅默认输出上限的一半，避免饱和窗口过大 */
    pid->integral_max = (out_max > 0.0f) ? (out_max * 0.5f) : (-out_min * 0.5f);
}

float PID_Compute(PID_Controller *pid, float measured)
{
    pid->error = pid->setpoint - measured;

    /* 积分累加 + 抗饱和 */
    pid->integral += pid->error;
    if (pid->integral >  pid->integral_max) pid->integral =  pid->integral_max;
    if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;

    pid->output = pid->Kp * pid->error
                + pid->Ki * pid->integral
                + pid->Kd * (pid->error - pid->last_error);

    /* 输出限幅 */
    if (pid->output > pid->output_max) pid->output = pid->output_max;
    if (pid->output < pid->output_min) pid->output = pid->output_min;

    pid->last_error = pid->error;
    return pid->output;
}

float PID_Incremental(PID_Controller *pid, float measured)
{
    pid->error = pid->setpoint - measured;

    float delta = pid->Kp * (pid->error - pid->last_error)
                + pid->Ki *  pid->error
                + pid->Kd * (pid->error - 2.0f * pid->last_error + pid->prev_error);

    pid->output += delta;

    if (pid->output > pid->output_max) pid->output = pid->output_max;
    if (pid->output < pid->output_min) pid->output = pid->output_min;

    pid->prev_error = pid->last_error;
    pid->last_error = pid->error;
    return pid->output;
}

void PID_Reset(PID_Controller *pid)
{
    pid->error      = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->integral   = 0.0f;
    pid->output     = 0.0f;
}

void PID_SetParams(PID_Controller *pid, float kp, float ki, float kd)
{
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
}

void PID_SetTarget(PID_Controller *pid, float target)
{
    pid->setpoint = target;
}
