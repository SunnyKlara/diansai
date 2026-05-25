/**
 * @file    pwm_apf.c
 * @brief   APF H 桥 PWM 驱动 —— 占位实现
 *
 * 真机移植：将 _hal_xxx 替换为 CubeMX/HAL 调用即可。
 */

#include "pwm_apf.h"
#include "../config.h"

static volatile uint8_t  s_running = 0;
static volatile uint16_t s_ccr1    = 0;
static volatile uint16_t s_ccr2    = 0;

/* ========================================================================
 * HAL 钩子
 * ===================================================================== */

static void _hal_init(void)
{
    /* TODO:
     *   - MX_TIM1_Init() with center-aligned mode 1, ARR = PWM_PERIOD_TICKS,
     *     dead-time = PWM_DEADTIME_TICKS, CH1/CH2 互补 PWM
     */
}

static void _hal_pwm_start(void)
{
    /* TODO: HAL_TIM_PWM_Start(&htim1, CH1/CH1N/CH2/CH2N) + __HAL_TIM_MOE_ENABLE */
}

static void _hal_pwm_stop(void)
{
    /* TODO: HAL_TIM_PWM_Stop + __HAL_TIM_MOE_DISABLE */
}

static void _hal_set_ccr(uint16_t ccr1, uint16_t ccr2)
{
    /* TODO: TIM1->CCR1 = ccr1; TIM1->CCR2 = ccr2; */
    (void)ccr1; (void)ccr2;
}

/* ========================================================================
 * API
 * ===================================================================== */

void pwm_apf_init(void)
{
    s_running = 0;
    s_ccr1 = (uint16_t)(PWM_PERIOD_TICKS * APF_DUTY_CENTER);
    s_ccr2 = (uint16_t)(PWM_PERIOD_TICKS * (1.0f - APF_DUTY_CENTER));
    _hal_init();
}

void pwm_apf_start(void)
{
    /* 上电默认 50% 占空比，输出 0V */
    s_ccr1 = (uint16_t)(PWM_PERIOD_TICKS * APF_DUTY_CENTER);
    s_ccr2 = (uint16_t)(PWM_PERIOD_TICKS * (1.0f - APF_DUTY_CENTER));
    _hal_set_ccr(s_ccr1, s_ccr2);
    s_running = 1;
    _hal_pwm_start();
}

void pwm_apf_stop(void)
{
    _hal_pwm_stop();
    s_running = 0;
}

void pwm_apf_emergency_off(void)
{
    /* 直接清 MOE，不需要等当前周期结束 */
    _hal_pwm_stop();
    s_running = 0;
}

void pwm_apf_set_duty(float duty)
{
    if (s_running == 0) {
        return;
    }

    /* 限幅 */
    if (duty < APF_DUTY_MIN) duty = APF_DUTY_MIN;
    if (duty > APF_DUTY_MAX) duty = APF_DUTY_MAX;

    /* 单极性：CH1 = duty, CH2 = 1 - duty */
    s_ccr1 = (uint16_t)((float)PWM_PERIOD_TICKS * duty);
    s_ccr2 = (uint16_t)((float)PWM_PERIOD_TICKS * (1.0f - duty));
    _hal_set_ccr(s_ccr1, s_ccr2);
}
