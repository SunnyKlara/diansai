/**
 * @file    pwm_coil.c
 */
#include "pwm_coil.h"
#include "../config.h"

static volatile uint8_t s_running = 0;
static volatile uint16_t s_ccr[NUM_COILS] = {0};

void pwm_coil_init(void) { s_running = 0; }
void pwm_coil_start(void){ s_running = 1; }
void pwm_coil_stop(void) { s_running = 0; }

void pwm_coil_set(uint8_t idx, float duty)
{
    if ((!s_running) || (idx >= NUM_COILS)) return;
    if (duty < 0)     duty = 0;
    if (duty > 0.95f) duty = 0.95f;
    s_ccr[idx] = (uint16_t)(duty * (float)PWM_PERIOD_TICKS);
    /* 真机：写入 TIM1->CCRx / TIM8->CCR1 */
}

void pwm_coil_emergency_off(void)
{
    s_running = 0;
    for (uint8_t i = 0; i < NUM_COILS; i++) s_ccr[i] = 0;
}
