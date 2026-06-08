#ifndef __PID_H
#define __PID_H

#include <stdint.h>

/* PID控制器结构体 */
typedef struct {
    float Kp;           // 比例系数
    float Ki;           // 积分系数
    float Kd;           // 微分系数
    
    float setpoint;     // 目标值
    float error;        // 当前误差
    float last_error;   // 上次误差
    float prev_error;   // 上上次误差
    float integral;     // 积分累积
    
    float output;       // PID输出
    float output_max;   // 输出上限
    float output_min;   // 输出下限
    float integral_max; // 积分限幅
} PID_Controller;

/* 初始化PID控制器 */
void PID_Init(PID_Controller *pid, float kp, float ki, float kd,
              float out_min, float out_max);

/* 位置式PID计算 */
float PID_Compute(PID_Controller *pid, float measured);

/* 增量式PID计算（适合速度环） */
float PID_Incremental(PID_Controller *pid, float measured);

/* 重置PID状态 */
void PID_Reset(PID_Controller *pid);

/* 设置PID参数（方便调参） */
void PID_SetParams(PID_Controller *pid, float kp, float ki, float kd);

/* 设置目标值 */
void PID_SetTarget(PID_Controller *pid, float target);

#endif /* __PID_H */
