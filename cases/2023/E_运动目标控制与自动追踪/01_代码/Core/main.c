/**
 * @file    main.c
 * @brief   2023 E 运动目标控制 + 自动追踪 主程序
 *          编译时通过 -DLASER_RED / -DLASER_GREEN 区分两台 MCU
 */
#include "../config.h"
#include "../Drivers/servo_gimbal.h"
#include "../Drivers/openmv_proto.h"
#include "../Algorithm/path_gen.h"
#include "../Algorithm/track_pid.h"
#include <math.h>

static volatile uint32_t s_tick_ms = 0;

#ifdef LASER_RED
static volatile float s_t01 = 0.0f;
static volatile float s_rot_deg = 0.0f;
static volatile path_type_t s_path = PATH_BORDER;
#else
static track_pid_t s_track;
#endif

void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
#ifdef LASER_RED
    /* 红色：预设路径 → 舵机 */
    s_t01 += (float)4.0f * CTRL_PERIOD_S / PATH_PERIOD_S;
    if (s_t01 >= 4.0f) s_t01 -= 4.0f;
    float x, y; path_get_point(s_path, s_t01, s_rot_deg, &x, &y);
    float yaw, pit; path_xy_to_angles(x, y, SCREEN_DIST_M, &yaw, &pit);
    gimbal_set(yaw, pit);
#else
    /* 绿色：取 OpenMV → 追踪 PID → 舵机增量 */
    int16_t cx, cy;
    if (omv_get(&cx, &cy)) {
        float dyaw, dpit;
        track_update(&s_track, cx, cy, &dyaw, &dpit);
        float y, p; gimbal_get(&y, &p);
        gimbal_set(y + dyaw, p + dpit);
    }
#endif
}

int main(void)
{
    gimbal_init();
#ifndef LASER_RED
    omv_init();
    track_init(&s_track);
#endif

    while (1) {
        /* idle */
    }
}
