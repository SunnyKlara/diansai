/**
 * @file    config.h
 * @brief   2022 A 单相交流电子负载 —— 全局参数中心
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

#define SYS_CLOCK_HZ        170000000UL
#define CTRL_FREQ_HZ        20000U
#define CTRL_PERIOD_S       (1.0f / 20000.0f)
#define GRID_FREQ_HZ        50.0f
#define CYCLE_SAMPLES       400U          /* 20k / 50Hz                   */
#define QUARTER_DELAY       100U          /* 90° 延迟点数                 */

/* ===== PI 控制器 ===== */
#define KP_ID               5.0f
#define KI_ID               500.0f
#define KP_IQ               5.0f
#define KI_IQ               500.0f
#define ID_INTEG_MAX        0.80f
#define IQ_INTEG_MAX        0.80f

/* ===== SOGI-PLL ===== */
#define PLL_KP              100.0f
#define PLL_KI              5000.0f
#define PLL_FREQ_NOMINAL    314.159f
#define PLL_FREQ_MAX        340.0f        /* 54 Hz                         */
#define PLL_FREQ_MIN        290.0f        /* 46 Hz                         */
#define SOGI_K              1.414f

/* ===== 占空比限幅 ===== */
#define DUTY_MIN            0.05f
#define DUTY_MAX            0.95f

/* ===== ADC 校准 ===== */
#define V_GRID_K            0.0220f
#define V_GRID_OFFSET       2048.0f
#define I_GRID_K            0.00366f
#define I_GRID_OFFSET       2048.0f
#define VBUS_K              0.0220f
#define VOUT_K              0.0220f

/* ===== 设定 ===== */
#define COSPHI_MIN          0.50f
#define COSPHI_MAX          1.00f
#define I_AMP_DEFAULT_A     2.0f
#define U1_DEFAULT_V        30.0f

/* ===== 数学 ===== */
#define PI                  3.14159265f
#define TWO_PI              6.28318531f
#define SQRT2               1.41421356f

#endif
