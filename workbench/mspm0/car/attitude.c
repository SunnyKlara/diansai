/*
 * attitude.c - 六轴姿态解算实现 (纯算法层, 不依赖 HAL)
 *   PC 验证: 见同目录 test_attitude.c ——
 *     gcc -O2 -Wall -o t attitude.c test_attitude.c -lm && ./t
 *   (本文件已用 PC 单测验证核心数学; 上板闭环转角仍 // 待真机验证)
 */
#include "attitude.h"
#include <math.h>

#define RAD2DEG   57.295779513f

void attitude_init(attitude_t *a, float dt, float alpha)
{
    a->yaw = a->pitch = a->roll = 0.0f;
    a->gbias[0] = a->gbias[1] = a->gbias[2] = 0.0f;
    a->dt = dt;
    a->alpha = alpha;
    a->started = 0;
    a->bacc[0] = a->bacc[1] = a->bacc[2] = 0.0f;
    a->bcount = 0;
}

void attitude_reset(attitude_t *a)
{
    a->yaw = a->pitch = a->roll = 0.0f;
    a->started = 0;
}

void attitude_reset_yaw(attitude_t *a, float yaw0)
{
    a->yaw = yaw0;
}

void attitude_bias_start(attitude_t *a)
{
    a->bacc[0] = a->bacc[1] = a->bacc[2] = 0.0f;
    a->bcount = 0;
}

void attitude_bias_sample(attitude_t *a, const float gyro_dps[3])
{
    a->bacc[0] += gyro_dps[0];
    a->bacc[1] += gyro_dps[1];
    a->bacc[2] += gyro_dps[2];
    a->bcount++;
}

void attitude_bias_apply(attitude_t *a)
{
    if (a->bcount > 0) {
        a->gbias[0] = a->bacc[0] / (float)a->bcount;
        a->gbias[1] = a->bacc[1] / (float)a->bcount;
        a->gbias[2] = a->bacc[2] / (float)a->bcount;
    }
}

void attitude_update(attitude_t *a, const float gyro_dps[3], const float accel_g[3])
{
    /* 去零偏 */
    float gx = gyro_dps[0] - a->gbias[0];
    float gy = gyro_dps[1] - a->gbias[1];
    float gz = gyro_dps[2] - a->gbias[2];

    /* 陀螺积分(预测) */
    a->yaw += gz * a->dt;                 /* yaw 无绝对参考, 纯积分 */
    float roll_g  = a->roll  + gx * a->dt;
    float pitch_g = a->pitch + gy * a->dt;

    /* 加速度重力校正(仅在近似静态/无强线加速时可信) */
    float ax = accel_g[0], ay = accel_g[1], az = accel_g[2];
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm > 0.5f && norm < 1.5f) {
        float roll_acc  = atan2f(ay, az) * RAD2DEG;
        float pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;
        a->roll  = a->alpha * roll_g  + (1.0f - a->alpha) * roll_acc;
        a->pitch = a->alpha * pitch_g + (1.0f - a->alpha) * pitch_acc;
    } else {
        /* 强动态: 仅信陀螺积分, 本拍不做加速度校正 */
        a->roll  = roll_g;
        a->pitch = pitch_g;
    }
    a->started = 1;
}

float attitude_wrap180(float deg)
{
    while (deg >= 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}
