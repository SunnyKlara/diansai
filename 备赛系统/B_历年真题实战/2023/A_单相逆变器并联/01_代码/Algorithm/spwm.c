/**
 * @file    spwm.c
 * @brief   SPWM 实现（已去 HAL 依赖）
 */

#include "spwm.h"
#include "../config.h"
#include <math.h>

#define MAX_TABLE_LEN  800U
#define TWO_PI         6.28318530718f

static float    s_sine_table[MAX_TABLE_LEN];
static uint16_t s_table_length = 400u;
static volatile uint16_t s_table_index = 0u;

static void rebuild_table(uint16_t len)
{
    if (len > MAX_TABLE_LEN) len = MAX_TABLE_LEN;
    if (len < 20u) len = 20u;
    s_table_length = len;
    for (uint16_t i = 0; i < s_table_length; i++) {
        s_sine_table[i] = sinf(TWO_PI * (float)i / (float)s_table_length);
    }
}

void SPWM_Init(uint16_t table_len)
{
    s_table_index = 0u;
    rebuild_table(table_len);
}

float SPWM_GetNextValue(void)
{
    float v = s_sine_table[s_table_index];
    s_table_index++;
    if (s_table_index >= s_table_length) s_table_index = 0u;
    return v;
}

void SPWM_SetFrequency(float freq_hz)
{
    if (freq_hz < 1.0f) freq_hz = 1.0f;
    uint16_t new_len = (uint16_t)((float)PWM_FSW_HZ / freq_hz + 0.5f);
    rebuild_table(new_len);
    s_table_index = 0u;
}

void SPWM_Reset(void)
{
    s_table_index = 0u;
}
