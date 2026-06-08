/**
 * @file    pwm_chopper.c
 * @brief   HRTIM 4 通道 PWM 占位实现
 */

#include "pwm_chopper.h"
#include "../config.h"

static volatile uint8_t s_running = 0;

static void _hal_init(void) { /* CubeMX MX_HRTIM1_Init */ }
static void _hal_start(void){ /* HAL_HRTIM_WaveformOutputStart */ }
static void _hal_stop(void) { /* HAL_HRTIM_WaveformOutputStop */ }
static void _hal_set_ccr(float c1, float c2, float c3, float c4)
{
    (void)c1;(void)c2;(void)c3;(void)c4;
    /* 真机：HRTIM_TIMxCMP1R / CMP2R 写入 (uint32_t)(c · ARR) */
}

void pwm_chopper_init(void)        { s_running = 0; _hal_init(); }
void pwm_chopper_start(void)       { s_running = 1; _hal_start(); }
void pwm_chopper_stop(void)        { _hal_stop(); s_running = 0; }
void pwm_chopper_emergency_off(void){ _hal_stop(); s_running = 0; }

void pwm_chopper_set(const chop_pwm_t *p)
{
    if ((p == 0) || (s_running == 0)) return;
    _hal_set_ccr(p->ccr1, p->ccr2, p->ccr3, p->ccr4);
}
