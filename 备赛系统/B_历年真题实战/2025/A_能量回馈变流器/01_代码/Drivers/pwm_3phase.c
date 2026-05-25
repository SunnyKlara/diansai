/**
 * @file    pwm_3phase.c
 * @brief   三相互补 PWM 启停封装实现
 */

#include "pwm_3phase.h"
#include "../config.h"

/* TODO_HAL: #include "stm32g4xx_hal.h" */
/* TODO_HAL: extern TIM_HandleTypeDef htim1; */

void PWM3Phase_Init(void)
{
    /* TODO_HAL:
     *   1) TIM1 中心对齐 PWM Mode 1，ARR=PWM_PERIOD
     *   2) DTG=DEAD_TIME_DTG，对应 800 ns 死区
     *   3) 三通道 + 三互补通道（CCxN）
     *   4) BDTR.MOE 默认 0（关断），由 PWM3Phase_Start 拉高
     *   5) 启用更新中断（用于 SVPWM 角度推进）
     */
}

void PWM3Phase_Start(void)
{
    /* TODO_HAL:
     *   __HAL_TIM_MOE_ENABLE(&htim1);
     *   HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
     *   HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
     *   ... 三通道
     */
}

void PWM3Phase_Stop(void)
{
    /* TODO_HAL: 立即关 MOE，所有 PWM 引脚拉低（含 N 通道）*/
    /* __HAL_TIM_MOE_DISABLE(&htim1); */
}

void PWM3Phase_SetDuty(float Ta, float Tb, float Tc)
{
    if (Ta < 0.0f) Ta = 0.0f; if (Ta > 1.0f) Ta = 1.0f;
    if (Tb < 0.0f) Tb = 0.0f; if (Tb > 1.0f) Tb = 1.0f;
    if (Tc < 0.0f) Tc = 0.0f; if (Tc > 1.0f) Tc = 1.0f;

    uint16_t ccr_a = (uint16_t)(Ta * (float)PWM_PERIOD);
    uint16_t ccr_b = (uint16_t)(Tb * (float)PWM_PERIOD);
    uint16_t ccr_c = (uint16_t)(Tc * (float)PWM_PERIOD);

    /* TODO_HAL:
     *   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr_a);
     *   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccr_b);
     *   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ccr_c);
     */
    (void)ccr_a; (void)ccr_b; (void)ccr_c;
}

void PWM3Phase_EmergencyOff(void)
{
    /* TODO_HAL: 同 PWM3Phase_Stop，但要求中断中可调用（不能阻塞）*/
    /* TIM1->BDTR &= ~TIM_BDTR_MOE; */
}
