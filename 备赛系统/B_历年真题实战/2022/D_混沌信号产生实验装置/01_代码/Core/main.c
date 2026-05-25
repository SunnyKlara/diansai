/**
 * @file    main.c
 * @brief   蔡氏混沌振荡 + DAC 双通道输出
 */
#include "../config.h"
#include "../Drivers/dac_dual.h"
#include "../Algorithm/chua_oscillator.h"

static chua_t s_chua;

static uint16_t scale_to_dac(float v, float v_min, float v_max)
{
    if (v < v_min) v = v_min;
    if (v > v_max) v = v_max;
    return (uint16_t)((v - v_min) / (v_max - v_min) * (float)DAC_MAX);
}

void main_dac_isr(void)
{
    chua_step(&s_chua);
    /* x ∈ [-3,3] → DAC 0..4095 */
    uint16_t dx = scale_to_dac(s_chua.x, -3.0f, 3.0f);
    uint16_t dy = scale_to_dac(s_chua.y, -3.0f, 3.0f);
    dac_dual_set(dx, dy);
}

int main(void)
{
    dac_dual_init();
    chua_init(&s_chua, CHUA_ALPHA, CHUA_BETA, CHUA_GAMMA,
              CHUA_M0, CHUA_M1, CHUA_DT);
    while (1) { /* TIM6 50kHz ISR 调 main_dac_isr */ }
}
