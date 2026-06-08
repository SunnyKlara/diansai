/**
 * @file    line_drive.c
 * @brief   直线段控制实现
 */

#include "line_drive.h"
#include "pid.h"
#include "../config.h"

static PID_Controller s_yaw_pid;
static float          s_target_dist_mm = 0.0f;
static float          s_setpoint_yaw   = 0.0f;
static LineDriveState s_state          = LINE_DONE;

/* yaw 误差归一化到 [-180, 180]，避免跨 ±180 导致控制器抖动 */
static float yaw_diff(float setpoint, float current)
{
    float e = setpoint - current;
    while (e >  180.0f) e -= 360.0f;
    while (e < -180.0f) e += 360.0f;
    return e;
}

void LineDrive_Begin(float distance_mm, float hold_yaw_deg)
{
    PID_Init(&s_yaw_pid, PID_YAW_KP, PID_YAW_KI, PID_YAW_KD,
             PID_YAW_OUT_MIN, PID_YAW_OUT_MAX);
    PID_SetTarget(&s_yaw_pid, 0.0f);   /* 我们直接喂误差进去（compute 时把 measured 设为 -e）*/

    s_target_dist_mm = distance_mm;
    s_setpoint_yaw   = hold_yaw_deg;
    s_state          = LINE_RUNNING;
}

LineDriveState LineDrive_Update(float yaw_deg, float trip_mm,
                                float *tgt_v_left, float *tgt_v_right)
{
    if (s_state == LINE_DONE) {
        if (tgt_v_left)  *tgt_v_left  = 0.0f;
        if (tgt_v_right) *tgt_v_right = 0.0f;
        return s_state;
    }

    /* ---- 距离判停 ---- */
    if (trip_mm >= s_target_dist_mm) {
        s_state = LINE_DONE;
        if (tgt_v_left)  *tgt_v_left  = 0.0f;
        if (tgt_v_right) *tgt_v_right = 0.0f;
        return s_state;
    }

    /* ---- 末段减速 ---- */
    float remaining = s_target_dist_mm - trip_mm;
    float base_pwm  = (remaining < V_DECEL_DIST_MM) ? (float)V_LINE_END_PWM
                                                    : (float)V_LINE_PWM;

    /* ---- 航向 PID（差速量）---- */
    float yaw_err  = yaw_diff(s_setpoint_yaw, yaw_deg);
    /* 用 PID_Compute(measured)：让 setpoint=0，measured=-yaw_err，则 e = setpoint - measured = yaw_err */
    float diff_pwm = PID_Compute(&s_yaw_pid, -yaw_err);

    /* ---- 输出 ---- */
    if (tgt_v_left)  *tgt_v_left  = base_pwm + diff_pwm;
    if (tgt_v_right) *tgt_v_right = base_pwm - diff_pwm;

    return s_state;
}

uint8_t LineDrive_IsDone(void)
{
    return (s_state == LINE_DONE) ? 1u : 0u;
}

void LineDrive_Reset(void)
{
    PID_Reset(&s_yaw_pid);
    s_state          = LINE_DONE;
    s_target_dist_mm = 0.0f;
}
