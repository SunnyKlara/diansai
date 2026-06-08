/**
 * @file    voltage_loop.h
 * @brief   三相输出电压外环 PI（线电压 RMS 闭环）
 *
 *  目标：输出线电压 RMS = VOUT_LINE_REF_V (32V)
 *  反馈：三相 ADC 采样 → RMS 计算 → 与 setpoint 比较
 *  输出：调制比 m（送给 SVPWM）
 *  周期：10 ms（VLOOP_PERIOD_MS）
 *
 *  策略：
 *      m_ff = Vref / (Vdc/√3) × √2     (前馈，加速响应)
 *      err = Vref - Vmeasured
 *      m   = m_ff + Kp × err + Ki × ∫err   (含抗饱和 + 限幅)
 */

#ifndef __VOLTAGE_LOOP_H
#define __VOLTAGE_LOOP_H

#include <stdint.h>

typedef struct {
    float setpoint;        /* 线电压 RMS 设定值 */
    float integral;        /* 积分累积 */
    float last_output;     /* 上次输出（监控用）*/
    uint8_t enabled;       /* 0 = 开环（用 m_ff），1 = 闭环 */
} VoltageLoop;

/**
 * @brief 初始化（清状态 + 设置 setpoint）
 */
void VLoop_Init(VoltageLoop *vl, float setpoint);

/**
 * @brief 使能 / 失能闭环（软启动期间应失能，避免积分饱和）
 */
void VLoop_Enable(VoltageLoop *vl, uint8_t enabled);

/**
 * @brief 修改 setpoint（频率切换时输出电压保持不变，所以一般不变）
 */
void VLoop_SetTarget(VoltageLoop *vl, float setpoint);

/**
 * @brief 周期更新（10 ms 调用）
 * @param vrms_meas  实测线电压 RMS
 * @param vdc        当前母线电压（用于前馈）
 * @return 调制比 m（已限幅到 [MOD_INDEX_MIN, MOD_INDEX_MAX]）
 */
float VLoop_Update(VoltageLoop *vl, float vrms_meas, float vdc);

/**
 * @brief 重置积分项（故障 / 停机时）
 */
void VLoop_Reset(VoltageLoop *vl);

#endif /* __VOLTAGE_LOOP_H */
