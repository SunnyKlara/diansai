/**
 * @file    path_gen.c
 * @brief   路径生成：边线 / A4 靶纸（带旋转）
 */
#include "path_gen.h"
#include "../config.h"
#include <math.h>

void path_get_point(path_type_t type, float t01, float rot_deg,
                    float *x, float *y)
{
    if (!x || !y) return;
    float xr = 0, yr = 0;
    float half = (type == PATH_BORDER) ? BORDER_HALF_M : A4_HALF_M;

    /* t01 ∈ [0, 4)：分四段，每段一条边 */
    float t = t01 - floorf(t01 / 4.0f) * 4.0f;
    if (t < 1.0f) { xr = -half + 2.0f * half * t;       yr =  half; }
    else if (t < 2.0f) { xr =  half;                    yr =  half - 2.0f * half * (t - 1.0f); }
    else if (t < 3.0f) { xr =  half - 2.0f * half * (t - 2.0f); yr = -half; }
    else { xr = -half;                                 yr = -half + 2.0f * half * (t - 3.0f); }

    /* 旋转 rot_deg */
    float c = cosf(rot_deg * (float)M_PI / 180.0f);
    float s = sinf(rot_deg * (float)M_PI / 180.0f);
    *x =  xr * c - yr * s;
    *y =  xr * s + yr * c;
}

void path_xy_to_angles(float x_m, float y_m, float dist_m,
                       float *yaw_deg, float *pitch_deg)
{
    if (!yaw_deg || !pitch_deg) return;
    *yaw_deg   = atanf(x_m / dist_m) * 180.0f / (float)M_PI;
    *pitch_deg = atanf(y_m / dist_m) * 180.0f / (float)M_PI;
}
