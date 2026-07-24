/*
 * control.h - 通用离散 PID 控制器 (供电流环/速度环/位置环复用)
 * 按固定节拍调用 pid_step(); Ki/Kd 已折算为"每拍"增益(不显式传 dt, 节拍固定即可)。
 *   out = clamp( Kp*err + integ + (-Kd*Δmeas) )
 *   integ += Ki*err   (条件抗积分饱和)
 *   微分对"测量值"求导(-Kd*Δmeas), 避免目标突变时的微分踢。
 */
#ifndef CONTROL_H
#define CONTROL_H

typedef struct {
    float kp, ki, kd;   /* 增益(Ki/Kd 为每拍) */
    float integ;        /* 积分累加(输出单位) */
    float prev_meas;    /* 上一拍测量(算微分用) */
    float out_min;      /* 输出下限 */
    float out_max;      /* 输出上限 */
    int   started;      /* 0=首拍(不算微分) */
} pid_t;

void  pid_init(pid_t *c, float kp, float ki, float kd, float out_min, float out_max);
void  pid_reset(pid_t *c);                              /* 清积分 + 复位微分(换目标/停机时用) */
void  pid_set_gains(pid_t *c, float kp, float ki, float kd);  /* 在线调参 */
float pid_step(pid_t *c, float setpoint, float meas);   /* 走一拍, 返回限幅后输出 */

#endif /* CONTROL_H */
