/**
 * @file    pid.h
 * @brief   通用 PID 控制器（位置式 + 增量式）
 * @note    复用自 24/C/Algorithm/pid，接口未改，仅注释强化。
 *          位置式：output = Kp*e + Ki*∫e + Kd*de/dt（带积分限幅 + 输出限幅）
 *          增量式：Δoutput = Kp*(e-e1) + Ki*e + Kd*(e-2*e1+e2)
 */

#ifndef __PID_H
#define __PID_H

#include <stdint.h>

typedef struct {
    /* 参数 */
    float Kp;
    float Ki;
    float Kd;

    /* 状态 */
    float setpoint;
    float error;          /* e[k]   */
    float last_error;     /* e[k-1] */
    float prev_error;     /* e[k-2] */
    float integral;
    float output;

    /* 限幅 */
    float output_min;
    float output_max;
    float integral_max;
} PID_Controller;

/**
 * @brief 初始化 PID 控制器
 */
void  PID_Init(PID_Controller *pid, float kp, float ki, float kd,
               float out_min, float out_max);

/**
 * @brief 位置式 PID 计算
 * @note  适用：航向环、循迹环、原地转向
 */
float PID_Compute(PID_Controller *pid, float measured);

/**
 * @brief 增量式 PID 计算
 * @note  适用：速度环（不容易积分饱和、对系统模型不敏感）
 */
float PID_Incremental(PID_Controller *pid, float measured);

/**
 * @brief 重置内部状态（不改 Kp/Ki/Kd）
 */
void  PID_Reset(PID_Controller *pid);

/**
 * @brief 运行时改 Kp/Ki/Kd（蓝牙/串口调参用）
 */
void  PID_SetParams(PID_Controller *pid, float kp, float ki, float kd);

/**
 * @brief 改 setpoint
 */
void  PID_SetTarget(PID_Controller *pid, float target);

#endif /* __PID_H */
