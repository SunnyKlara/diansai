/**
 * @file    config.h
 * @brief   2024 A AC-AC 变换电路并联 —— 全局参数中心
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

/* ========== 系统时基 ========== */
#define SYS_CLOCK_HZ            170000000UL  /* STM32G474 主频              */
#define PWM_FREQ_HZ             20000U
#define ZCD_FREQ_HZ             50U
#define VRMS_LOOP_HZ            100U          /* 10ms 电压外环               */

/* ========== 电源参数 ========== */
#define VIN_NOMINAL_VAC         36.0f
#define VOUT_MIN_V              1.0f
#define VOUT_MAX_V              35.0f
#define VOUT_STEP_V             0.5f
#define IOUT_MAX_A              4.0f

/* ========== 占空比限幅 ========== */
#define DUTY_MIN                0.03f
#define DUTY_MAX                0.97f

/* ========== 四步换流时序（HRTIM ticks，1 tick = 1/170MHz ≈ 5.88ns）======== */
#define COMM_STEP_NS            1000U
#define COMM_STEP_TICKS         170U          /* 1 μs                        */

/* ========== 电压闭环 PI ========== */
#define KP_VOUT                 0.012f
#define KI_VOUT                 0.005f
#define VOUT_INTEG_MAX          0.95f
#define VOUT_INTEG_MIN          0.05f

/* ========== 下垂控制 ========== */
#define DROOP_R_OHM             0.10f         /* 输出虚拟电阻               */

/* ========== ADC 校准 ========== */
#define ADC_VREF                3.3f
#define ADC_RES                 4096.0f
#define VIN_SCALE_V_PER_LSB     0.0220f
#define VOUT_SCALE_V_PER_LSB    0.0214f
#define IOUT_SCALE_A_PER_LSB    0.00366f      /* ACS712-5A 185mV/A          */
#define IOUT_OFFSET_LSB         2048.0f

/* ========== 保护 ========== */
#define IOUT_OVER_A             6.0f
#define VOUT_OVER_V             40.0f
#define VOUT_UNDER_V            0.5f

/* ========== 数学 ========== */
#define PI                      3.14159265f
#define TWO_PI                  6.28318531f
#define SQRT2                   1.41421356f
#define INV_SQRT2               0.70710678f

#endif /* __CONFIG_H__ */
