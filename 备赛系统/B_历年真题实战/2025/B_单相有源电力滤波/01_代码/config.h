/**
 * @file    config.h
 * @brief   2025 B 单相有源电力滤波装置 —— 全局可调参数中心
 * @note    所有 PI 系数 / 阈值 / 采样率 / ADC 校准参数集中此处，
 *          其他模块只能引用，不允许在 .c 中定义同名宏。
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

/* ========================================================================
 * 1. 系统时基与采样
 * ===================================================================== */

#define SYS_CLOCK_HZ            168000000UL  /* STM32F407 主频 168 MHz       */

#define APF_CTRL_FREQ_HZ        20000U       /* APF 电流环 20 kHz             */
#define APF_CTRL_PERIOD_S       (1.0f / (float)APF_CTRL_FREQ_HZ)

#define HARMONIC_SAMPLE_HZ      10000U       /* 谐波 FFT 采样 10 kHz          */
#define HARMONIC_FFT_SIZE       1024U        /* 1024 点 FFT                  */

#define GRID_FREQ_HZ            50.0f        /* 工频 50 Hz                   */
#define APF_CYCLE_SAMPLES       (APF_CTRL_FREQ_HZ / 50U)  /* 一周期 400 点 */

#define LCD_REFRESH_HZ          5U           /* LCD 5 Hz 刷新（200ms）         */
#define BUTTON_SCAN_HZ          50U          /* 按键 50 Hz 扫描                */

/* ========================================================================
 * 2. ADC 校准与调理
 * ===================================================================== */

#define ADC_RESOLUTION          4096.0f      /* 12 bit ADC 量程 0..4095       */
#define ADC_VREF                3.3f         /* 参考电压 3.3 V                 */

/* uS（电网电压）通道：ZMPT101B 互感器 + 调理 → ADC IN0 */
#define ADC_US_OFFSET_LSB       2048.0f      /* 单极性偏置 1.65V → 2048      */
#define ADC_US_SCALE_V_PER_LSB  0.0244f      /* 1 LSB ≈ 24.4mV (实测校准)    */
#define ADC_US_GAIN_V_PER_V     0.05f        /* 24Vac → 1.2V 调理电路系数    */

/* iL（负载电流）通道：ZMCT103C(1000:1) + 100Ω 取样 → ADC IN1 */
#define ADC_IL_OFFSET_LSB       2048.0f
#define ADC_IL_SCALE_A_PER_LSB  0.00244f     /* 1 LSB ≈ 2.44 mA (实测校准)   */

/* iF（APF 输出电流）通道：内置霍尔 → ADC IN2 */
#define ADC_IF_OFFSET_LSB       2048.0f
#define ADC_IF_SCALE_A_PER_LSB  0.00244f

/* 自动零点校准：开机静态采样 N 点取平均 */
#define ADC_AUTO_ZERO_SAMPLES   256U

/* ========================================================================
 * 3. APF 电流跟踪 PI 控制器
 * ===================================================================== */

#define APF_KP                  5.0f         /* 比例系数：起步值              */
#define APF_KI                  500.0f       /* 积分系数：起步值              */
#define APF_DUTY_CENTER         0.5f         /* 单极性 PWM 中点              */
#define APF_DUTY_MIN            0.05f        /* 占空比下限（防完全关断）      */
#define APF_DUTY_MAX            0.95f        /* 占空比上限                    */
#define APF_INTEG_MAX           0.40f        /* 积分项限幅（防 windup）       */

/* 软启动 ramp：1 秒内从 0% → 100% 系数 */
#define APF_SOFT_START_TIME_S   1.0f
#define APF_SOFT_START_STEP     (APF_CTRL_PERIOD_S / APF_SOFT_START_TIME_S)

/* ========================================================================
 * 4. PWM 与死区
 * ===================================================================== */

#define PWM_FREQ_HZ             APF_CTRL_FREQ_HZ      /* 20 kHz             */
#define PWM_PERIOD_TICKS        (SYS_CLOCK_HZ / 2U / PWM_FREQ_HZ) /* 中心对齐 */

/* 死区：500 ns @ 168 MHz → ticks = 500e-9 × 168e6 = 84 */
#define PWM_DEADTIME_NS         500U
#define PWM_DEADTIME_TICKS      84U

/* ========================================================================
 * 5. 谐波分析（基本要求 (3) 核心）
 * ===================================================================== */

/* 50 Hz × 1024 / 10kHz = 5.12，整周期采样取 5 → 实际窗内 5.12 个周期，
 * 必须加汉宁窗校正泄漏。                                              */
#define FUND_BIN_INDEX          5U           /* H₁ 在第 5 bin              */
#define HARMONIC_MAX_ORDER      10U          /* 计算到 10 次谐波             */
#define HANN_WINDOW_GAIN        2.0f         /* 汉宁窗能量补偿 ×2           */

#define THD_MIN_FUND_A          0.001f       /* 基波 < 1mA 时 THD 置 0       */

/* ========================================================================
 * 6. 保护阈值
 * ===================================================================== */

#define VBUS_OVER_V             55.0f        /* 母线过压 55V                  */
#define VBUS_UNDER_V            38.0f        /* 母线欠压 38V                  */
#define VBUS_PRECHARGE_V        43.0f        /* 预充完成阈值（0.9×48）         */

#define IF_MAX_INSTANT_A        6.0f         /* APF 瞬时过流 6A                */
#define IL_MAX_RMS_A            5.0f         /* 负载电流 RMS 上限              */

/* ========================================================================
 * 7. 状态机超时
 * ===================================================================== */

#define PRECHARGE_TIMEOUT_MS    3000U        /* 预充 3 秒超时               */
#define FAULT_RECOVER_MS        500U         /* 故障后 500ms 才允许重试       */

/* ========================================================================
 * 8. 数学常量
 * ===================================================================== */

#ifndef PI
#define PI                      3.14159265358979f
#endif
#define TWO_PI                  6.28318530717958f
#define SQRT2                   1.41421356f
#define INV_SQRT2               0.70710678f

#endif /* __CONFIG_H__ */
