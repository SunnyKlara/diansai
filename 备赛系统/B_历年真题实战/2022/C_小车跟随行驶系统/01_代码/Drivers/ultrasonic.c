/**
 * @file    ultrasonic.c
 * @brief   HC-SR04 占位 + 中值滤波
 */
#include "ultrasonic.h"
#include "../config.h"

static float s_buf[ULTRA_FILTER_LEN];
static uint8_t s_idx = 0;
static volatile float s_dist_cm = 20.0f;

void ultra_init(void) { for (uint8_t i = 0; i < ULTRA_FILTER_LEN; i++) s_buf[i] = 20.0f; }

void ultra_trigger(void) { /* trig pin 10us */ }

void ultra_on_echo_isr(uint32_t pulse_us)
{
    /* 距离（cm）= pulse_us × 0.0343 / 2 */
    float d = (float)pulse_us * 0.0172f;
    if (d > 200.0f) d = 200.0f;
    s_buf[s_idx++] = d;
    if (s_idx >= ULTRA_FILTER_LEN) s_idx = 0;
    /* 简单平均 */
    float sum = 0;
    for (uint8_t i = 0; i < ULTRA_FILTER_LEN; i++) sum += s_buf[i];
    s_dist_cm = sum / (float)ULTRA_FILTER_LEN;
}

float ultra_get_cm(void) { return s_dist_cm; }
