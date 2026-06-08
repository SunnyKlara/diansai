#include "pid.h"

/**
 * @brief 初始化PID控制器
 */
void PID_Init(PID_Controller *pid, float kp, float ki, float kd,
              float out_min, float out_max)
{
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->setpoint = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->integral = 0.0f;
    pid->output = 0.0f;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->integral_max = out_max * 0.5f;  // 积分限幅为输出上限的一半
}

/**
 * @brief 位置式PID计算
 * @note  适合转向环（方向PID）
 *        output = Kp*e + Ki*∫e + Kd*de/dt
 */
float PID_Compute(PID_Controller *pid, float measured)
{
    pid->error = pid->setpoint - measured;
    
    /* 积分累积 + 抗积分饱和 */
    pid->integral += pid->error;
    if (pid->integral > pid->integral_max)
        pid->integral = pid->integral_max;
    else if (pid->integral < -pid->integral_max)
        pid->integral = -pid->integral_max;
    
    /* PID计算 */
    pid->output = pid->Kp * pid->error
                + pid->Ki * pid->integral
                + pid->Kd * (pid->error - pid->last_error);
    
    /* 输出限幅 */
    if (pid->output > pid->output_max)
        pid->output = pid->output_max;
    else if (pid->output < pid->output_min)
        pid->output = pid->output_min;
    
    /* 保存历史误差 */
    pid->last_error = pid->error;
    
    return pid->output;
}

/**
 * @brief 增量式PID计算
 * @note  适合速度环
 *        Δoutput = Kp*(e-e1) + Ki*e + Kd*(e-2*e1+e2)
 */
float PID_Incremental(PID_Controller *pid, float measured)
{
    pid->error = pid->setpoint - measured;
    
    float increment = pid->Kp * (pid->error - pid->last_error)
                    + pid->Ki * pid->error
                    + pid->Kd * (pid->error - 2.0f * pid->last_error + pid->prev_error);
    
    pid->output += increment;
    
    /* 输出限幅 */
    if (pid->output > pid->output_max)
        pid->output = pid->output_max;
    else if (pid->output < pid->output_min)
        pid->output = pid->output_min;
    
    /* 保存历史误差 */
    pid->prev_error = pid->last_error;
    pid->last_error = pid->error;
    
    return pid->output;
}

/**
 * @brief 重置PID状态
 */
void PID_Reset(PID_Controller *pid)
{
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->integral = 0.0f;
    pid->output = 0.0f;
}

/**
 * @brief 运行时修改PID参数
 */
void PID_SetParams(PID_Controller *pid, float kp, float ki, float kd)
{
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
}

/**
 * @brief 设置目标值
 */
void PID_SetTarget(PID_Controller *pid, float target)
{
    pid->setpoint = target;
}
