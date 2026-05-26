/**
 * @file    pwm_3phase.c
 * @brief   STM32G474 三相互补 PWM 启停封装实现（TIM1 + 死区 + BKIN）
 *
 * 资源映射：
 *   TIM1_CH1  (PA8 ) → A 相上管
 *   TIM1_CH1N (PB13) → A 相下管
 *   TIM1_CH2  (PA9 ) → B 相上管
 *   TIM1_CH2N (PB14) → B 相下管
 *   TIM1_CH3  (PA10) → C 相上管
 *   TIM1_CH3N (PB15) → C 相下管
 *   TIM1_BKIN (PA12) → 硬件保护输入
 *
 *   死区：DEAD_TIME_DTG = 0x55（500ns @ 170MHz）
 */

#include "pwm_3phase.h"
#include "../config.h"
#include "stm32g4xx_hal.h"

extern TIM_HandleTypeDef htim1;

void PWM3Phase_Init(void)
{
    /* CubeMX 已配置：
     *   TIM1 中心对齐 PWM Mode 1, ARR = PWM_PERIOD - 1
     *   死区 DTG = 0x55, BDTR.MOE 默认 0
     *   三通道 + 三互补通道
     *   BKIN PA12 使能，触发后自动关 MOE
     *   TRGO = Update Event（驱动 ADC + SVPWM 角度推进）
     */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);

    /* 强制关 MOE 防上电误输出 */
    __HAL_TIM_MOE_DISABLE(&htim1);
}

void PWM3Phase_Start(void)
{
    HAL_TIM_PWM_Start (&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start (&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start (&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);

    __HAL_TIM_MOE_ENABLE(&htim1);
}

void PWM3Phase_Stop(void)
{
    __HAL_TIM_MOE_DISABLE(&htim1);

    HAL_TIM_PWM_Stop (&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop (&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Stop (&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
}

void PWM3Phase_SetDuty(float Ta, float Tb, float Tc)
{
    if (Ta < 0.0f) Ta = 0.0f; if (Ta > 1.0f) Ta = 1.0f;
    if (Tb < 0.0f) Tb = 0.0f; if (Tb > 1.0f) Tb = 1.0f;
    if (Tc < 0.0f) Tc = 0.0f; if (Tc > 1.0f) Tc = 1.0f;

    uint16_t ccr_a = (uint16_t)(Ta * (float)PWM_PERIOD);
    uint16_t ccr_b = (uint16_t)(Tb * (float)PWM_PERIOD);
    uint16_t ccr_c = (uint16_t)(Tc * (float)PWM_PERIOD);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr_a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccr_b);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ccr_c);
}

void PWM3Phase_EmergencyOff(void)
{
    /* 紧急关断：直接寄存器清 MOE，1 个时钟周期内 6 路引脚拉低。 */
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}
