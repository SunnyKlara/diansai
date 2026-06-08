/**
 * @file    config.h
 * @brief   2021 B 三相 AC-DC 变换电路 —— 全局参数中心
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

#define SYS_CLOCK_HZ          170000000UL
#define VIENNA_FSW_HZ         50000U
#define BUCK_FSW_HZ           100000U
#define CTRL_FREQ_HZ          50000U      /* DQ 电流环周期 50kHz       */
#define VOUT_LOOP_HZ          1000U
#define VBUS_LOOP_HZ          200U

/* ===== 输入 380V 三相 50Hz ===== */
#define VIN_LL_VAC            380.0f
#define GRID_FREQ_HZ          50.0f

/* ===== 输出 ===== */
#define VOUT_NOMINAL_V        36.0f
#define VOUT_TOL_V            0.1f
#define IOUT_MAX_A            2.0f

/* ===== 母线 ===== */
#define VBUS_NOMINAL_V        700.0f      /* ±350V 分裂                */
#define VBUS_OVER_V           780.0f
#define VBUS_UNDER_V          550.0f
#define VBUS_PRECHARGE_V      650.0f

/* ===== Vienna DQ 控制 ===== */
#define VIENNA_KP_ID          0.6f
#define VIENNA_KI_ID          400.0f
#define VIENNA_KP_IQ          0.6f
#define VIENNA_KI_IQ          400.0f
#define VIENNA_KP_VBUS        2.0f
#define VIENNA_KI_VBUS        100.0f

/* ===== Buck 控制 ===== */
#define BUCK_KP_VOUT          0.05f
#define BUCK_KI_VOUT          200.0f
#define BUCK_KP_IL            1.5f
#define BUCK_KI_IL            500.0f
#define BUCK_DUTY_MIN         0.05f
#define BUCK_DUTY_MAX         0.92f

/* ===== Vienna 调制 ===== */
#define VIENNA_M_MAX          0.95f       /* 调制指数上限              */

/* ===== ADC ===== */
#define ADC_VPHASE_K          0.0973f     /* 相电压 LSB→V              */
#define ADC_IPHASE_K          0.00366f
#define ADC_VBUS_K            0.215f
#define ADC_VOUT_K            0.0220f
#define ADC_IOUT_K            0.00366f

/* ===== 数学 ===== */
#define PI                    3.14159265f
#define TWO_PI                6.28318531f
#define SQRT3                 1.73205081f
#define INV_SQRT3             0.57735027f
#define SQRT2                 1.41421356f
#define TWO_PI_OVER_3         2.09439510f

#endif
