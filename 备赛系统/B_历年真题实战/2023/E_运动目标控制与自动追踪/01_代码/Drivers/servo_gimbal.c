/**
 * @file    servo_gimbal.c
 * @brief   两轴舵机云台 PWM 占位（MG996R, 50Hz）
 */
#include "servo_gimbal.h"
#include "../config.h"

static volatile float s_yaw_deg = 0, s_pitch_deg = 0;

void gimbal_init(void) { s_yaw_deg = 0; s_pitch_deg = 0; }

void gimbal_set(float yaw_deg, float pitch_deg)
{
    if (yaw_deg < SERVO_YAW_MIN_DEG)   yaw_deg = SERVO_YAW_MIN_DEG;
    if (yaw_deg > SERVO_YAW_MAX_DEG)   yaw_deg = SERVO_YAW_MAX_DEG;
    if (pitch_deg < SERVO_PITCH_MIN_DEG) pitch_deg = SERVO_PITCH_MIN_DEG;
    if (pitch_deg > SERVO_PITCH_MAX_DEG) pitch_deg = SERVO_PITCH_MAX_DEG;
    s_yaw_deg = yaw_deg; s_pitch_deg = pitch_deg;
    /* 真机：TIM PWM CCR = 500 + (deg / 180) * 2000  → 0.5..2.5ms / 20ms */
}

void gimbal_get(float *yaw_deg, float *pitch_deg)
{
    if (yaw_deg)   *yaw_deg   = s_yaw_deg;
    if (pitch_deg) *pitch_deg = s_pitch_deg;
}
