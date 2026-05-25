/**
 * @file    svpwm_3phase.c
 * @brief   三相空间矢量调制（SVPWM）核心算法
 *
 *  设计：
 *      - 与硬件完全解耦（用 PWM3Phase_SetDuty 写占空比）
 *      - 不依赖 HAL 库 / arm_math.h（用 math.h 标准 sin/cos）
 *      - 七段式分配，谐波最优
 *
 *  扇区判断（标准方法）：
 *      U1 = Uβ
 *      U2 = (√3/2)·Uα - (1/2)·Uβ
 *      U3 = -(√3/2)·Uα - (1/2)·Uβ
 *      A=sign(U1), B=sign(U2), C=sign(U3)
 *      N = 4C + 2B + A → 查表得扇区 1~6
 */

#include "svpwm_3phase.h"
#include "../config.h"
#include "../Drivers/pwm_3phase.h"

#include <math.h>

#define SQRT3      1.7320508f
#define TWO_PI     6.2831853f
#define HALF_SQRT3 0.8660254f

/* ---- 模块状态 ---- */
static float s_theta      = 0.0f;
static float s_omega      = TWO_PI * 50.0f;     /* 50Hz 默认 */
static float s_mod_index  = 0.80f;
static float s_vdc        = VDC_NOMINAL_V;

/* ============================================================
 *  公开 API
 * ============================================================ */
void SVPWM_Init(void)
{
    s_theta     = 0.0f;
    s_omega     = TWO_PI * (float)FREQ_OUT_DEFAULT_HZ;
    s_mod_index = (float)MOD_INDEX_INIT;
    s_vdc       = (float)VDC_NOMINAL_V;
}

void SVPWM_SetFrequency(float freq_hz)
{
    if (freq_hz < (float)FREQ_OUT_MIN_HZ) freq_hz = (float)FREQ_OUT_MIN_HZ;
    if (freq_hz > (float)FREQ_OUT_MAX_HZ) freq_hz = (float)FREQ_OUT_MAX_HZ;
    s_omega = TWO_PI * freq_hz;
}

void SVPWM_SetAmplitude(float mod_index)
{
    if (mod_index < (float)MOD_INDEX_MIN) mod_index = (float)MOD_INDEX_MIN;
    if (mod_index > (float)MOD_INDEX_MAX) mod_index = (float)MOD_INDEX_MAX;
    s_mod_index = mod_index;
}

void SVPWM_SetVdc(float vdc)
{
    if (vdc < 5.0f) vdc = 5.0f;     /* 防 SVPWM_Calc 内部除零 */
    s_vdc = vdc;
}

/* ============================================================
 *  SVPWM 核心算法
 * ============================================================ */
SVPWM_Out SVPWM_Calc(float Ualpha, float Ubeta, float Udc)
{
    SVPWM_Out out;
    out.sector = 0;
    out.Ta = out.Tb = out.Tc = 0.5f;

    if (Udc < 1.0f) return out;

    /* 1. 扇区判断 */
    float U1 = Ubeta;
    float U2 = HALF_SQRT3 * Ualpha - 0.5f * Ubeta;
    float U3 = -HALF_SQRT3 * Ualpha - 0.5f * Ubeta;

    uint8_t A = (U1 > 0.0f) ? 1u : 0u;
    uint8_t B = (U2 > 0.0f) ? 1u : 0u;
    uint8_t C = (U3 > 0.0f) ? 1u : 0u;
    uint8_t N = (uint8_t)((C << 2) | (B << 1) | A);

    static const uint8_t kSectorTable[8] = {0, 2, 6, 1, 4, 3, 5, 0};
    out.sector = kSectorTable[N];

    /* 2. 计算两个相邻基本矢量的作用时间（归一化到 [0, 1]）
     *    K = √3 / Udc
     */
    float K = SQRT3 / Udc;
    float T1 = 0.0f, T2 = 0.0f;

    switch (out.sector) {
        case 1:
            T1 = K * (HALF_SQRT3 * Ualpha - 0.5f * Ubeta);
            T2 = K * Ubeta;
            break;
        case 2:
            T1 = K * (HALF_SQRT3 * Ualpha + 0.5f * Ubeta);
            T2 = K * (-HALF_SQRT3 * Ualpha + 0.5f * Ubeta);
            break;
        case 3:
            T1 = K * Ubeta;
            T2 = K * (-HALF_SQRT3 * Ualpha - 0.5f * Ubeta);
            break;
        case 4:
            T1 = -K * Ubeta;
            T2 = K * (-HALF_SQRT3 * Ualpha + 0.5f * Ubeta);
            break;
        case 5:
            T1 = K * (-HALF_SQRT3 * Ualpha - 0.5f * Ubeta);
            T2 = K * (HALF_SQRT3 * Ualpha - 0.5f * Ubeta);
            break;
        case 6:
            T1 = K * (-HALF_SQRT3 * Ualpha + 0.5f * Ubeta);
            T2 = -K * Ubeta;
            break;
        default: break;
    }

    /* 过调制处理 */
    if ((T1 + T2) > 1.0f) {
        float s = 1.0f / (T1 + T2);
        T1 *= s;
        T2 *= s;
    }
    float T0_half = (1.0f - T1 - T2) * 0.5f;

    /* 3. 七段式占空比分配 */
    switch (out.sector) {
        case 1:
            out.Ta = T1 + T2 + T0_half;
            out.Tb = T2 + T0_half;
            out.Tc = T0_half;
            break;
        case 2:
            out.Ta = T1 + T0_half;
            out.Tb = T1 + T2 + T0_half;
            out.Tc = T0_half;
            break;
        case 3:
            out.Ta = T0_half;
            out.Tb = T1 + T2 + T0_half;
            out.Tc = T2 + T0_half;
            break;
        case 4:
            out.Ta = T0_half;
            out.Tb = T1 + T0_half;
            out.Tc = T1 + T2 + T0_half;
            break;
        case 5:
            out.Ta = T2 + T0_half;
            out.Tb = T0_half;
            out.Tc = T1 + T2 + T0_half;
            break;
        case 6:
            out.Ta = T1 + T2 + T0_half;
            out.Tb = T0_half;
            out.Tc = T1 + T0_half;
            break;
        default: break;
    }

    return out;
}

/* ============================================================
 *  顶层逆变器更新（20kHz 中断中调用）
 * ============================================================ */
void Inverter3Ph_Update(void)
{
    /* 1. 推进电角度 */
    s_theta += s_omega * (float)DT_PWM_S;
    if (s_theta >= TWO_PI) s_theta -= TWO_PI;
    if (s_theta < 0.0f)    s_theta += TWO_PI;

    /* 2. 计算目标电压矢量 (αβ)
     *    Vref_peak = m × Vdc / √3   （SVPWM 线性范围内）
     */
    float Vref = s_mod_index * s_vdc / SQRT3;
    float Ualpha = Vref * cosf(s_theta);
    float Ubeta  = Vref * sinf(s_theta);

    /* 3. SVPWM 计算 */
    SVPWM_Out pwm = SVPWM_Calc(Ualpha, Ubeta, s_vdc);

    /* 4. 写入 PWM（解耦：通过 driver 接口）*/
    PWM3Phase_SetDuty(pwm.Ta, pwm.Tb, pwm.Tc);
}
