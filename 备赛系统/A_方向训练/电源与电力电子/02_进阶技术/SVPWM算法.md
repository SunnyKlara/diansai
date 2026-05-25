# SVPWM空间矢量调制 —— 三相逆变必备

## 一、为什么需要SVPWM

| 对比 | SPWM | SVPWM |
|------|------|-------|
| 直流电压利用率 | 0.5 (50%) | 0.577 (57.7%) |
| 谐波含量 | 较高 | 较低 |
| 适用场景 | 单相逆变 | **三相逆变** |
| 实现复杂度 | 简单 | 中等 |

SVPWM比SPWM多利用15.5%的直流母线电压，对三相逆变器来说非常重要。

## 二、原理简述

三相全桥逆变器有6个开关管，8种开关状态（含2个零矢量）。
SVPWM的核心：在每个PWM周期内，用相邻的两个基本矢量和零矢量合成目标矢量。

## 三、STM32实现代码

```c
/**
 * SVPWM算法实现
 * 输入：Uα, Uβ (Clark变换后的两相静止坐标)
 * 输出：三相PWM占空比 Ta, Tb, Tc
 */

#include <math.h>

#define SQRT3   1.7320508f
#define T_PWM   4250       // PWM周期计数值 (对应20kHz@170MHz/2)

typedef struct {
    float Ta, Tb, Tc;      // 三相占空比 (0~1)
    uint8_t sector;         // 所在扇区 (1~6)
} SVPWM_Output_t;

SVPWM_Output_t svpwm_calc(float Ualpha, float Ubeta, float Udc)
{
    SVPWM_Output_t out;
    
    // ===== 1. 判断扇区 =====
    float U1 = Ubeta;
    float U2 = (SQRT3 * Ualpha - Ubeta) / 2.0f;
    float U3 = (-SQRT3 * Ualpha - Ubeta) / 2.0f;
    
    uint8_t A = (U1 > 0) ? 1 : 0;
    uint8_t B = (U2 > 0) ? 1 : 0;
    uint8_t C = (U3 > 0) ? 1 : 0;
    uint8_t N = 4*C + 2*B + A;
    
    // N到扇区的映射
    const uint8_t sector_table[8] = {0, 2, 6, 1, 4, 3, 5, 0};
    out.sector = sector_table[N];
    
    // ===== 2. 计算基本矢量作用时间 =====
    float T1, T2, T0;
    float K = SQRT3 / Udc;  // 归一化系数
    
    switch (out.sector) {
        case 1: T1 = K * (SQRT3/2*Ualpha - 0.5f*Ubeta);
                T2 = K * Ubeta; break;
        case 2: T1 = K * (SQRT3/2*Ualpha + 0.5f*Ubeta);
                T2 = K * (-SQRT3/2*Ualpha + 0.5f*Ubeta); break;
        case 3: T1 = K * Ubeta;
                T2 = K * (-SQRT3/2*Ualpha - 0.5f*Ubeta); break;
        case 4: T1 = K * (-Ubeta);
                T2 = K * (-SQRT3/2*Ualpha + 0.5f*Ubeta); break; // 修正
        case 5: T1 = K * (-SQRT3/2*Ualpha - 0.5f*Ubeta);
                T2 = K * (SQRT3/2*Ualpha - 0.5f*Ubeta); break;
        case 6: T1 = K * (-SQRT3/2*Ualpha + 0.5f*Ubeta);
                T2 = K * (-Ubeta); break;
        default: T1 = 0; T2 = 0; break;
    }
    
    // 过调制处理
    if (T1 + T2 > 1.0f) {
        float ratio = 1.0f / (T1 + T2);
        T1 *= ratio;
        T2 *= ratio;
    }
    T0 = 1.0f - T1 - T2;
    
    // ===== 3. 计算三相占空比 (七段式) =====
    float Ta_on, Tb_on, Tc_on;
    
    switch (out.sector) {
        case 1: Ta_on = T1 + T2 + T0/2; Tb_on = T2 + T0/2; Tc_on = T0/2; break;
        case 2: Ta_on = T1 + T0/2; Tb_on = T1 + T2 + T0/2; Tc_on = T0/2; break;
        case 3: Ta_on = T0/2; Tb_on = T1 + T2 + T0/2; Tc_on = T2 + T0/2; break;
        case 4: Ta_on = T0/2; Tb_on = T1 + T0/2; Tc_on = T1 + T2 + T0/2; break;
        case 5: Ta_on = T2 + T0/2; Tb_on = T0/2; Tc_on = T1 + T2 + T0/2; break;
        case 6: Ta_on = T1 + T2 + T0/2; Tb_on = T0/2; Tc_on = T1 + T0/2; break;
        default: Ta_on = 0.5f; Tb_on = 0.5f; Tc_on = 0.5f; break;
    }
    
    out.Ta = Ta_on;
    out.Tb = Tb_on;
    out.Tc = Tc_on;
    
    return out;
}

// ===== 使用示例 =====
// 在定时器中断中调用 (20kHz)

void TIM1_UP_IRQHandler(void) {
    static float theta = 0;
    float omega = 2 * 3.14159f * 50.0f;  // 50Hz输出
    float dt = 1.0f / 20000.0f;          // 20kHz
    
    theta += omega * dt;
    if (theta > 2 * 3.14159f) theta -= 2 * 3.14159f;
    
    // 目标电压矢量 (旋转)
    float Ualpha = Vref * arm_cos_f32(theta);
    float Ubeta  = Vref * arm_sin_f32(theta);
    
    // SVPWM计算
    SVPWM_Output_t pwm = svpwm_calc(Ualpha, Ubeta, Vdc);
    
    // 更新三相PWM
    TIM1->CCR1 = (uint16_t)(pwm.Ta * T_PWM);
    TIM1->CCR2 = (uint16_t)(pwm.Tb * T_PWM);
    TIM1->CCR3 = (uint16_t)(pwm.Tc * T_PWM);
}
```

## 四、在电赛中的应用
- **2025A**：三相逆变器，SVPWM是首选调制方式
- **2021B**：三相AC-DC，PFC控制中也用到SVPWM
- 掌握SVPWM是做三相电源题的前提条件
