/**
 * @file    voltage_loop.c
 * @brief   电压外环 PI 实现
 */

#include "voltage_loop.h"
#include "../config.h"
#include <math.h>

#define SQRT3   1.7320508f
#define SQRT2   1.4142136f

/* 前馈调制比：把 Vline_rms 反推为 SVPWM 调制比
 * Vphase_peak = m × Vdc/√3
 * Vline_rms   = √3/√2 × Vphase_peak = √3/√2 × m × Vdc/√3 = m × Vdc/√2
 * → m = Vline_rms × √2 / Vdc
 */
static float feedforward_mod(float vref_line, float vdc)
{
    if (vdc < 5.0f) return MOD_INDEX_INIT;     /* 母线电压异常时返回安全默认 */
    float m = vref_line * SQRT2 / vdc;
    if (m > MOD_INDEX_MAX) m = MOD_INDEX_MAX;
    if (m < MOD_INDEX_MIN) m = MOD_INDEX_MIN;
    return m;
}

void VLoop_Init(VoltageLoop *vl, float setpoint)
{
    if (!vl) return;
    vl->setpoint = setpoint;
    vl->integral = MOD_INDEX_INIT;
    vl->last_output = MOD_INDEX_INIT;
    vl->enabled = 0;
}

void VLoop_Enable(VoltageLoop *vl, uint8_t enabled)
{
    if (!vl) return;
    if (!vl->enabled && enabled) {
        /* 闭环刚开启，让积分项从当前输出开始（避免突变）*/
        vl->integral = vl->last_output;
    }
    vl->enabled = enabled;
}

void VLoop_SetTarget(VoltageLoop *vl, float setpoint)
{
    if (!vl) return;
    vl->setpoint = setpoint;
}

float VLoop_Update(VoltageLoop *vl, float vrms_meas, float vdc)
{
    if (!vl) return MOD_INDEX_INIT;

    /* 前馈调制比 */
    float m_ff = feedforward_mod(vl->setpoint, vdc);

    if (!vl->enabled) {
        vl->last_output = m_ff;
        return m_ff;
    }

    /* 闭环：以前馈为基础叠加 PI */
    float err = vl->setpoint - vrms_meas;

    /* I 项 + 抗饱和 */
    vl->integral += VLOOP_KI * err;
    if (vl->integral > VLOOP_INT_MAX) vl->integral = VLOOP_INT_MAX;
    if (vl->integral < VLOOP_INT_MIN) vl->integral = VLOOP_INT_MIN;

    float m = m_ff + VLOOP_KP * err + (vl->integral - m_ff) * 0.0f /* 仅作占位 */;
    /* 注：这里 integral 直接作为基础项（与原代码一致），
     * m_ff 用于初始/失能时返回。具体调参时可改为：
     *   m = vl->integral + VLOOP_KP * err；
     * 取决于 KI 的整定方法。*/
    m = vl->integral + VLOOP_KP * err;

    if (m > MOD_INDEX_MAX) m = MOD_INDEX_MAX;
    if (m < MOD_INDEX_MIN) m = MOD_INDEX_MIN;

    vl->last_output = m;
    return m;
}

void VLoop_Reset(VoltageLoop *vl)
{
    if (!vl) return;
    vl->integral = MOD_INDEX_INIT;
    vl->last_output = MOD_INDEX_INIT;
    vl->enabled = 0;
}
