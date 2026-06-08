/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ          168000000UL
#define CTRL_FREQ_HZ          2000U
#define CTRL_PERIOD_S         (1.0f / 2000.0f)
#define PWM_FREQ_HZ           20000U
#define PWM_PERIOD_TICKS      4200U

/* 电磁铁阵列 */
#define NUM_COILS             5
#define NUM_HALL              5
#define CAL_POINTS            10

/* PD 控制 */
#define MAGLEV_KP_CENTER      15.0f
#define MAGLEV_KD_CENTER      0.8f
#define MAGLEV_KI_CENTER      0.10f

#define MAGLEV_KP_SIDE        4.0f
#define MAGLEV_KD_SIDE        0.20f
#define MAGLEV_KI_SIDE        0.0f

/* 高度设定 */
#define H_REF_DEFAULT_MM      20.0f
#define H_REF_MIN_MM          10.0f
#define H_REF_MAX_MM          30.0f
#define H_RAMP_STEP_MM        0.20f
#define H_RAMP_DT_MS          10U

/* 保护 */
#define DUTY_MAX              0.95f

#endif
