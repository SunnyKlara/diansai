/**
 * @file    pwm_full_bridge.c
 * @brief   全桥 PWM 实现（封装 TIM1 操作）
 */

#include "pwm_full_bridge.h"
#include "../config.h"

/* TODO_HAL: #include "stm32g4xx_hal.h" */
/* TODO_HAL: extern TIM_HandleTypeDef htim1; */

static int16_t clamp_duty(float duty)
{
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    return (int16_t)(duty * (float)PWM_PERIOD);
}

void PWMFB_Init(void)
{
    /* TODO_HAL:
     *   1) TIM1 中心对齐 PWM Mode 1, ARR = PWM_PERIOD
     *   2) DTG = DEAD_TIME_DTG (800ns)
     *   3) CH1/CH1N + CH2/CH2N 四路输出，CH1 = A 桥臂高侧，CH1N = A 桥臂低侧
     *   4) BDTR.MOE 默认 0，由 PWMFB_Start 拉高
     *   5) 启用更新中断 → 每开关周期推进 SPWM 角度
     */
}

void PWMFB_Start(void)
{
    /* TODO_HAL:
     *   __HAL_TIM_MOE_ENABLE(&htim1);
     *   HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
     *   HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
     *   HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
     *   HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
     */
}

void PWMFB_Stop(void)
{
    /* TODO_HAL: 清 BDTR.MOE，所有引脚拉低 */
}

void PWMFB_SetDuty(float duty_a, float duty_b)
{
    int16_t ccr_a = clamp_duty(duty_a);
    int16_t ccr_b = clamp_duty(duty_b);

    /* TODO_HAL:
     *   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr_a);
     *   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccr_b);
     */
    (void)ccr_a;
    (void)ccr_b;
}

void PWMFB_EmergencyOff(void)
{
    /* TODO_HAL: TIM1->BDTR &= ~TIM_BDTR_MOE; */
}
