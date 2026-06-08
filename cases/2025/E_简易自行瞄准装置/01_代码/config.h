/**
 * @file    config.h
 * @brief   2025 E 简易自行瞄准装置 —— 全局参数中心
 *          覆盖小车与瞄准两块 MCU（同一仓库共用 config）
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

/* ======================== 共享 ======================== */
#define UART_BAUD             115200U
#define MSG_CAR_READY         0xFFU         /* 小车 → 瞄准：就位                */
#define MSG_AIM_DONE          0xAAU         /* 瞄准 → 小车：命中                 */

/* ======================== 小车 MCU ===================== */
#define CAR_SYS_CLOCK_HZ      168000000UL
#define CAR_CTRL_HZ           100U

#define CAR_V_CRUISE_MPS      0.6f
#define CAR_V_TURN_MPS        0.25f
#define CAR_V_APPROACH_MPS    0.10f

#define CAR_LINE_KP           20.0f
#define CAR_LINE_KD           80.0f

#define CAR_VEL_KP            12.0f
#define CAR_VEL_KI            60.0f

#define CAR_IR_NUM            5U
#define CAR_IR_TH_LOW         1500
#define CAR_IR_TH_HIGH        2500

#define CAR_WHEEL_DIAM_MM     67.0f
#define CAR_ENCODER_PPR       11U
#define CAR_GEAR_RATIO        30U

/* ======================== 瞄准 MCU ===================== */
#define AIM_SYS_CLOCK_HZ      72000000UL
#define AIM_CTRL_HZ           100U

#define AIM_KP_X              0.06f
#define AIM_KP_Y              0.06f
#define AIM_KD_X              0.012f
#define AIM_KD_Y              0.012f

#define IMG_W                 160
#define IMG_H                 120
#define IMG_CX                80
#define IMG_CY                60

#define AIM_TARGET_TOL_PX     2

#define SERVO_YAW_MIN_DEG    -45.0f
#define SERVO_YAW_MAX_DEG     45.0f
#define SERVO_PITCH_MIN_DEG  -30.0f
#define SERVO_PITCH_MAX_DEG   30.0f

#define PI                    3.14159265f

#endif
