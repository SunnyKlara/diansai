/**
 * @file    track_pid.c
 * @brief   绿色云台追踪 PID（视觉像素 → 舵机增量）
 */
#include "track_pid.h"
#include "../config.h"

static float clamp(float x, float lo, float hi){ if(x<lo)return lo;if(x>hi)return hi;return x; }

void track_init(track_pid_t *p)
{
    if (!p) return;
    p->kp_x = TRACK_KP_X; p->kd_x = TRACK_KD_X;
    p->kp_y = TRACK_KP_Y; p->kd_y = TRACK_KD_Y;
    p->prev_ex = 0; p->prev_ey = 0;
}

void track_update(track_pid_t *p, int16_t cx, int16_t cy,
                  float *yaw_inc_deg, float *pitch_inc_deg)
{
    if (!p || !yaw_inc_deg || !pitch_inc_deg) return;
    float ex = (float)(cx - IMG_CENTER_X);
    float ey = (float)(cy - IMG_CENTER_Y);
    float dex = ex - p->prev_ex; p->prev_ex = ex;
    float dey = ey - p->prev_ey; p->prev_ey = ey;
    *yaw_inc_deg   = clamp(p->kp_x * ex + p->kd_x * dex, -3.0f, 3.0f);
    *pitch_inc_deg = clamp(p->kp_y * ey + p->kd_y * dey, -3.0f, 3.0f);
}
