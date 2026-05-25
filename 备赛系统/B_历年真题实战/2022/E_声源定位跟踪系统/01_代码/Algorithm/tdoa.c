/**
 * @file    tdoa.c
 * @brief   TDOA 声源定位 —— 互相关 + 几何
 */
#include "tdoa.h"
#include "../config.h"
#include <math.h>

void tdoa_init(void) {}

/* 互相关找 i,j 通道延迟（采样点数，正/负）*/
static int32_t xcorr_lag(const int16_t *a, const int16_t *b, uint32_t N, int32_t max_lag)
{
    int32_t best_lag = 0;
    float best_sum = 0;
    for (int32_t lag = -max_lag; lag <= max_lag; lag++) {
        float sum = 0;
        for (uint32_t i = 0; i < N; i++) {
            int32_t j = (int32_t)i + lag;
            if (j < 0 || j >= (int32_t)N) continue;
            sum += (float)a[i] * (float)b[j];
        }
        if (sum > best_sum) { best_sum = sum; best_lag = lag; }
    }
    return best_lag;
}

void tdoa_locate(const int16_t *m1, const int16_t *m2,
                 const int16_t *m3, const int16_t *m4,
                 uint32_t len, float fs_hz,
                 tdoa_result_t *out)
{
    if (!m1 || !m2 || !m3 || !m4 || !out) return;

    int32_t max_lag = (int32_t)(MIC_DIST_M / SOUND_SPEED * fs_hz) + 5;

    int32_t lag12 = xcorr_lag(m1, m2, len, max_lag);
    int32_t lag14 = xcorr_lag(m1, m4, len, max_lag);

    /* 时差转角度（远场近似 + 麦克风对在 x/y 轴）*/
    float dt12 = (float)lag12 / fs_hz;
    float dt14 = (float)lag14 / fs_hz;

    float dx = SOUND_SPEED * dt12 / MIC_DIST_M;       /* 在 [-1, 1] */
    float dy = SOUND_SPEED * dt14 / MIC_DIST_M;
    if (dx < -1.0f) dx = -1.0f; if (dx > 1.0f) dx = 1.0f;
    if (dy < -1.0f) dy = -1.0f; if (dy > 1.0f) dy = 1.0f;

    out->angle_deg = atan2f(dy, dx) * 180.0f / 3.14159265f;
    out->x_m = dx * 1.0f;
    out->y_m = dy * 1.0f;
    out->dist_m = sqrtf(out->x_m * out->x_m + out->y_m * out->y_m) * 5.0f;
    out->quality = 0.9f;
}
