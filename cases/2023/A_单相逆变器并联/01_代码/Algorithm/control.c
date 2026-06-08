/**
 * @file    control.c
 * @brief   电压外环 PI 实现（已去 HAL 依赖）
 */

#include "control.h"
#include "../config.h"

static float s_vref     = VOUT_REF_V;
static float s_integral = MOD_INDEX_INIT;

void Control_Init(float vref)
{
    s_vref     = vref;
    s_integral = MOD_INDEX_INIT;
}

void Control_SetVref(float vref)
{
    s_vref = vref;
}

float Control_VoltageLoop(float vout_rms)
{
    float err = s_vref - vout_rms;

    /* 积分项 + 抗饱和 */
    s_integral += VLOOP_KI * err;
    if (s_integral > MOD_INDEX_MAX) s_integral = MOD_INDEX_MAX;
    if (s_integral < MOD_INDEX_MIN) s_integral = MOD_INDEX_MIN;

    /* PI 输出 */
    float m = VLOOP_KP * err + s_integral;
    if (m > MOD_INDEX_MAX) m = MOD_INDEX_MAX;
    if (m < MOD_INDEX_MIN) m = MOD_INDEX_MIN;

    return m;
}

void Control_Reset(void)
{
    s_integral = MOD_INDEX_INIT;
}
