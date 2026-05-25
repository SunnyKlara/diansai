/**
 * 2025A 三相SVPWM逆变器控制
 * MCU: STM32G474RET6
 * 
 * 功能：
 *   - SVPWM空间矢量调制，产生三相对称正弦交流电
 *   - 输出频率20~100Hz可调（步进1Hz）
 *   - 输出电压闭环稳压（线电压32V±0.25V）
 *   - THD≤2%
 * 
 * 硬件连接：
 *   TIM1_CH1/CH1N -> A相桥臂 (PA8/PA7)
 *   TIM1_CH2/CH2N -> B相桥臂 (PA9/PB0)
 *   TIM1_CH3/CH3N -> C相桥臂 (PA10/PB1)
 *   ADC1_CH1~CH3  -> 三相输出电压采样
 *   ADC2_CH1~CH3  -> 三相输出电流采样
 */

#include "svpwm_3phase.h"
#include "arm_math.h"

/* ========== 参数 ========== */
#define FSW             20000       // 开关频率 20kHz
#define SQRT3           1.7320508f
#define TWO_PI          6.2831853f
#define PWM_PERIOD      4250        // 170MHz / 2 / 20kHz = 4250

/* ========== 状态变量 ========== */
static float theta = 0;             // 电角度 (rad)
static float omega = TWO_PI * 50.0f; // 角频率 (rad/s)，初始50Hz
static float mod_index = 0.8f;      // 调制比
static float Vdc = 48.0f;           // 母线电压

/* ========== SVPWM核心算法 ========== */

SVPWM_Out_t SVPWM_Calc(float Ualpha, float Ubeta, float Udc)
{
    SVPWM_Out_t out;
    
    // 1. 扇区判断
    float U1 = Ubeta;
    float U2 = (SQRT3 * Ualpha - Ubeta) * 0.5f;
    float U3 = (-SQRT3 * Ualpha - Ubeta) * 0.5f;
    
    uint8_t A = (U1 > 0) ? 1 : 0;
    uint8_t B = (U2 > 0) ? 1 : 0;
    uint8_t C = (U3 > 0) ? 1 : 0;
    uint8_t N = 4*C + 2*B + A;
    
    const uint8_t sector_table[8] = {0, 2, 6, 1, 4, 3, 5, 0};
    out.sector = sector_table[N];
    
    // 2. 计算矢量作用时间
    float T1 = 0, T2 = 0;
    float K = SQRT3 / Udc;
    
    switch (out.sector) {
        case 1: T1 = K*(SQRT3*0.5f*Ualpha - 0.5f*Ubeta);
                T2 = K*Ubeta; break;
        case 2: T1 = K*(SQRT3*0.5f*Ualpha + 0.5f*Ubeta);
                T2 = K*(-SQRT3*0.5f*Ualpha + 0.5f*Ubeta); break;
        case 3: T1 = K*Ubeta;
                T2 = K*(-SQRT3*0.5f*Ualpha - 0.5f*Ubeta); break;
        case 4: T1 = -K*Ubeta;
                T2 = K*(-SQRT3*0.5f*Ualpha + 0.5f*Ubeta); break;
        case 5: T1 = K*(-SQRT3*0.5f*Ualpha - 0.5f*Ubeta);
                T2 = K*(SQRT3*0.5f*Ualpha - 0.5f*Ubeta); break;
        case 6: T1 = K*(-SQRT3*0.5f*Ualpha + 0.5f*Ubeta);
                T2 = -K*Ubeta; break;
        default: T1 = 0; T2 = 0; break;
    }
    
    // 过调制处理
    if (T1 + T2 > 1.0f) {
        float s = 1.0f / (T1 + T2);
        T1 *= s; T2 *= s;
    }
    float T0 = 1.0f - T1 - T2;
    
    // 3. 七段式PWM分配
    switch (out.sector) {
        case 1: out.Ta = T1+T2+T0*0.5f; out.Tb = T2+T0*0.5f;    out.Tc = T0*0.5f; break;
        case 2: out.Ta = T1+T0*0.5f;    out.Tb = T1+T2+T0*0.5f; out.Tc = T0*0.5f; break;
        case 3: out.Ta = T0*0.5f;       out.Tb = T1+T2+T0*0.5f; out.Tc = T2+T0*0.5f; break;
        case 4: out.Ta = T0*0.5f;       out.Tb = T1+T0*0.5f;    out.Tc = T1+T2+T0*0.5f; break;
        case 5: out.Ta = T2+T0*0.5f;    out.Tb = T0*0.5f;       out.Tc = T1+T2+T0*0.5f; break;
        case 6: out.Ta = T1+T2+T0*0.5f; out.Tb = T0*0.5f;       out.Tc = T1+T0*0.5f; break;
        default: out.Ta = 0.5f; out.Tb = 0.5f; out.Tc = 0.5f; break;
    }
    
    return out;
}

void SVPWM_Init(void)
{
    theta = 0;
    omega = TWO_PI * 50.0f;
    mod_index = 0.8f;
}

void SVPWM_SetFrequency(float freq_hz)
{
    if (freq_hz < 20.0f) freq_hz = 20.0f;
    if (freq_hz > 100.0f) freq_hz = 100.0f;
    omega = TWO_PI * freq_hz;
}

void SVPWM_SetAmplitude(float mod)
{
    if (mod < 0.1f) mod = 0.1f;
    if (mod > 0.95f) mod = 0.95f;
    mod_index = mod;
}

/**
 * 三相逆变器更新函数
 * 在TIM1更新中断中调用 (20kHz)
 */
extern TIM_HandleTypeDef htim1;

void Inverter3Ph_Update(void)
{
    // 1. 更新电角度
    float dt = 1.0f / (float)FSW;
    theta += omega * dt;
    if (theta >= TWO_PI) theta -= TWO_PI;
    
    // 2. 计算目标电压矢量 (αβ坐标)
    float Vref = mod_index * Vdc / SQRT3;  // SVPWM的电压幅值
    float Ualpha = Vref * arm_cos_f32(theta);
    float Ubeta  = Vref * arm_sin_f32(theta);
    
    // 3. SVPWM计算
    SVPWM_Out_t pwm = SVPWM_Calc(Ualpha, Ubeta, Vdc);
    
    // 4. 更新三相PWM
    htim1.Instance->CCR1 = (uint16_t)(pwm.Ta * PWM_PERIOD);
    htim1.Instance->CCR2 = (uint16_t)(pwm.Tb * PWM_PERIOD);
    htim1.Instance->CCR3 = (uint16_t)(pwm.Tc * PWM_PERIOD);
}
