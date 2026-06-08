/**
 * @file    config.h
 * @brief   2022 C 小车跟随行驶 —— 全局参数（TI MSP432 平台）
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        48000000UL    /* MSP432P401R 主频 48MHz       */
#define CTRL_HZ             100U
#define CTRL_PERIOD_S       0.01f

/* ===== 速度规划 ===== */
#define V_LEAD_SLOW_MPS     0.3f
#define V_LEAD_FAST_MPS     1.0f
#define V_OVERTAKE_MPS      0.6f

/* ===== 间距 PID ===== */
#define DIST_TARGET_CM      20.0f
#define DIST_TOL_CM         6.0f
#define DIST_KP             6.0f          /* mm/s per cm                  */
#define DIST_KI             0.5f
#define DIST_KD             1.5f

/* ===== 速度环 ===== */
#define VEL_KP              12.0f
#define VEL_KI              60.0f

/* ===== 循迹 PD ===== */
#define LINE_KP             20.0f
#define LINE_KD             80.0f

/* ===== 红外阈值 ===== */
#define IR_NUM              5
#define IR_TH_LOW           1500
#define IR_TH_HIGH          2500

/* ===== 超声波 ===== */
#define ULTRA_TIMEOUT_US    30000U
#define ULTRA_FILTER_LEN    5U

/* ===== 通信包 ===== */
#define NRF_HEADER          0xAAU
#define CMD_START           0x01U
#define CMD_STOP            0x02U
#define CMD_SPEED           0x03U
#define CMD_OVERTAKE        0x04U

#define PI                  3.14159265f

#endif
