/**
 * @file    tdoa.h
 * @brief   TDOA 声源定位（4 麦克风阵列）
 *          假设 2D 平面，麦克风分布在 (±D/2, ±D/2)
 */
#ifndef __TDOA_H__
#define __TDOA_H__
#include <stdint.h>

typedef struct {
    float x_m, y_m;       /* 声源平面坐标（m）          */
    float angle_deg;      /* 方位角（°）                */
    float dist_m;         /* 距阵列中心距离             */
    float quality;        /* 0..1 置信度                */
} tdoa_result_t;

void tdoa_init(void);

/**
 * 输入 4 麦克风同步采样数据，互相关法估计 TDOA → 解方程定位
 */
void tdoa_locate(const int16_t *m1, const int16_t *m2,
                 const int16_t *m3, const int16_t *m4,
                 uint32_t len, float fs_hz,
                 tdoa_result_t *out);
#endif
