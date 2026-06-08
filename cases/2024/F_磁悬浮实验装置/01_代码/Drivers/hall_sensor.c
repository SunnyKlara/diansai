/**
 * @file    hall_sensor.c
 * @brief   SS49E 线性霍尔 + 距离查表
 */
#include "hall_sensor.h"
#include "../config.h"

static const float CAL_V[CAL_POINTS]  = {1.5f,1.7f,1.9f,2.1f,2.3f,2.5f,2.7f,2.9f,3.1f,3.3f};
static const float CAL_MM[CAL_POINTS] = {50,40,32,26,21,17,14,11,9,7};

static volatile float s_dist[NUM_HALL] = {20,20,20,20,20};

void hall_init(void) {}

void hall_on_adc(const uint16_t *raw)
{
    if (!raw) return;
    for (uint8_t i = 0; i < NUM_HALL; i++) {
        float v = (float)raw[i] * (3.3f / 4096.0f);
        s_dist[i] = hall_v2mm(v);
    }
}

float hall_v2mm(float v)
{
    if (v <= CAL_V[0])              return CAL_MM[0];
    if (v >= CAL_V[CAL_POINTS - 1]) return CAL_MM[CAL_POINTS - 1];
    for (uint8_t i = 0; i < CAL_POINTS - 1; i++) {
        if ((v >= CAL_V[i]) && (v < CAL_V[i + 1])) {
            float t = (v - CAL_V[i]) / (CAL_V[i + 1] - CAL_V[i]);
            return CAL_MM[i] * (1.0f - t) + CAL_MM[i + 1] * t;
        }
    }
    return 20.0f;
}

float hall_get_mm(uint8_t idx)
{
    if (idx >= NUM_HALL) return 20.0f;
    return s_dist[idx];
}
