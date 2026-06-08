/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        72000000UL    /* STM32F103 主频 72MHz */
#define CTRL_HZ             100U
#define CTRL_PERIOD_S       0.01f

/* ===== 电机 ===== */
#define MOTOR_MAX_PWM       1000
#define WHEEL_DIAM_MM       67.0f
#define ENCODER_PPR         11
#define GEAR_RATIO          30

/* ===== 速度 ===== */
#define V_STRAIGHT_MPS      0.8f
#define V_TURN_MPS          0.3f
#define V_APPROACH_MPS      0.15f

/* ===== 循迹 PID ===== */
#define LINE_KP             18.0f
#define LINE_KD             80.0f
#define LINE_KI             0.05f

/* ===== 速度 PID ===== */
#define VEL_KP              15.0f
#define VEL_KI              50.0f
#define VEL_INTEG_MAX       300.0f

/* ===== 红外阈值 ===== */
#define IR_NUM              5
#define IR_THRESHOLD_HIGH   2500
#define IR_THRESHOLD_LOW    1500

/* ===== 病房布局 ===== */
#define ROOM_MAX            8

/* ===== 双车 ===== */
#define CAR_ID_SELF         1
#define CAR_ID_OTHER        2

#define PI                  3.14159265f

#endif
