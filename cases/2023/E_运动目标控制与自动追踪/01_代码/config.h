/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        72000000UL
#define CTRL_HZ             100U
#define CTRL_PERIOD_S       0.01f

/* ===== 几何 ===== */
#define SCREEN_DIST_M       1.0f          /* 屏幕距离 1m                  */
#define BORDER_HALF_M       0.25f         /* 边线 50×50cm 半边 25cm       */
#define A4_HALF_M           0.105f        /* A4 短边 21cm 半 10.5cm        */

/* ===== 舵机限位 ===== */
#define SERVO_YAW_MIN_DEG   -45.0f
#define SERVO_YAW_MAX_DEG    45.0f
#define SERVO_PITCH_MIN_DEG -30.0f
#define SERVO_PITCH_MAX_DEG  30.0f

/* ===== 追踪 PID ===== */
#define TRACK_KP_X          0.05f
#define TRACK_KI_X          0.0f
#define TRACK_KD_X          0.01f
#define TRACK_KP_Y          0.05f
#define TRACK_KI_Y          0.0f
#define TRACK_KD_Y          0.01f

/* ===== OpenMV 视觉 ===== */
#define IMG_CENTER_X        80
#define IMG_CENTER_Y        60
#define IMG_W               160
#define IMG_H               120

/* ===== 路径速度 ===== */
#define PATH_PERIOD_S       30.0f         /* 一圈 30s（基本要求）         */

#define PI                  3.14159265f

#endif
