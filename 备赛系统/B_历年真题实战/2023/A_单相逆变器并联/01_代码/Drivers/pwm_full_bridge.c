/**
 * @file    pwm_full_bridge.c
 * @brief   STM32G474 全桥 PWM 实现（TIM1 中心对齐 + 互补 + 死区）
 *
 * 资源映射（与 引脚分配表.md / 接线表.md 同步）：
 *   TIM1_CH1  (PA8)  → IR2110 #1 HIN  (M1 上管)
 *   TIM1_CH1N (PB13) → IR2110 #1 LIN  (M2 下管)
 *   TIM1_CH2  (PA9)  → IR2110 #2 HIN  (M3 上管)
 *   TIM1_CH2N (PB14) → IR2110 #2 LIN  (M4 下管)
 *   TIM1_BKIN (PA12) → 比较器输出 / 硬件保护
 *
 *   死区：DEAD_TIME_DTG = 0x88（约 800ns @ 170MHz）
 *
 * 编译条件：CubeMX 生成 main.h 中需有 htim1 句柄。
 */

#include "pwm_full_bridge.h"
#include "../config.h"
#include "stm32g4xx_hal.h"

extern TIM_HandleTypeDef htim1;

/* ========================================================================
 * 内部辅助
 * ===================================================================== */

static int16_t clamp_duty(float duty)
{
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    return (int16_t)(duty * (float)PWM_PERIOD);
}

/* ========================================================================
 * 公开 API
 * ===================================================================== */

void PWMFB_Init(void)
{
    /* CubeMX 已配置：
     *   TIM1 中心对齐 PWM Mode 1, ARR=PWM_PERIOD-1
     *   DTG=0x88（800ns @170MHz）, BDTR.MOE 默认 0
     *   CH1+CH1N + CH2+CH2N 互补输出
     *   BKIN（PA12）使能，AF 高有效
     *
     * 这里只做"开机即关 PWM" 安全初始化：
     */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);

    /* 显式关 MOE 防上电瞬间输出 */
    __HAL_TIM_MOE_DISABLE(&htim1);
}

void PWMFB_Start(void)
{
    /* 启动 4 路 PWM 输出 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);

    /* 最后开 MOE，所有引脚立即生效 */
    __HAL_TIM_MOE_ENABLE(&htim1);
}

void PWMFB_Stop(void)
{
    /* 先关 MOE → 引脚立即拉低 */
    __HAL_TIM_MOE_DISABLE(&htim1);

    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
}

void PWMFB_SetDuty(float duty_a, float duty_b)
{
    int16_t ccr_a = clamp_duty(duty_a);
    int16_t ccr_b = clamp_duty(duty_b);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)ccr_a);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (uint32_t)ccr_b);
}

void PWMFB_EmergencyOff(void)
{
    /* 紧急关断（在 ISR 内调用，不能阻塞）：
     *   直接清 BDTR.MOE 位，1 个时钟周期内 4 路引脚拉低。
     */
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}
