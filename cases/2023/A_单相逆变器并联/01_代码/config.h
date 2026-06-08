/**
 * @file    config.h
 * @brief   2023 A 单相逆变器并联系统 全局可调参数
 * @note    所有需要调试时修改的参数集中在这里。
 *          严禁在 .c 文件里硬编码"魔法数字"。
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>

/* ============================================================
 *  1. 系统时钟与开关频率
 * ============================================================ */
#define SYS_CLK_HZ              170000000UL    /* STM32G474 主频 */
#define PWM_FSW_HZ              20000U         /* SPWM 开关频率 20 kHz */
#define PWM_PERIOD              ((SYS_CLK_HZ / 2 / PWM_FSW_HZ) - 1)
                                               /* 中心对齐 ARR = 4249 */

/* 一个开关周期的时间 (s) */
#define DT_PWM_S                (1.0f / (float)PWM_FSW_HZ)

/* ============================================================
 *  2. 输出参数
 * ============================================================ */
#define VOUT_REF_V              24.0f          /* 目标输出电压 RMS */
#define VOUT_TOL_V              0.05f          /* 容差 0.2% × 24V */
#define IOUT_RATED_RMS_A        2.0f           /* 额定输出电流 RMS */
#define FREQ_OUT_HZ             50.0f
#define SAMPLES_PER_CYCLE       400U           /* 20kHz / 50Hz */

/* ============================================================
 *  3. 母线
 * ============================================================ */
#define VDC_NOMINAL_V           48.0f
#define VDC_MIN_V               36.0f
#define VDC_MAX_V               55.0f
#define VDC_PRECHARGE_V         (VDC_NOMINAL_V * 0.9f)

/* ============================================================
 *  4. PWM 死区
 * ============================================================ */
/* DTG 计算：fDTS = 170MHz/2 = 85MHz，DTG[7]=0 时单步 = 11.76 ns
 * 800 ns / 11.76 ns ≈ 68
 */
#define DEAD_TIME_NS            800
#define DEAD_TIME_DTG           68U

/* ============================================================
 *  5. 电压外环 PI
 * ============================================================ */
#define VLOOP_PERIOD_MS         10
#define VLOOP_KP                0.020f
#define VLOOP_KI                0.005f

#define MOD_INDEX_MIN           0.10f
#define MOD_INDEX_MAX           0.95f
#define MOD_INDEX_INIT          0.70f

/* 软启动：调制比从 MIN 升到 INIT 持续时间 */
#define RAMP_DURATION_MS        500
#define RAMP_STEP               (1.0f / (float)(RAMP_DURATION_MS / VLOOP_PERIOD_MS))

/* ============================================================
 *  6. ADC（STM32G4 ADC1）
 * ============================================================ */
#define ADC_RESOLUTION          4096U          /* 12bit */
#define ADC_VREF_V              3.3f
#define ADC_OFFSET_RAW          2048U          /* 1.65V 偏置 */

/* 电压采样：ZMPT 副边输出 + 100k/10k 分压（11:1）+ 1.65V 偏置 */
#define V_DIVIDER_RATIO         11.0f
#define V_SCALE                 (ADC_VREF_V / (float)ADC_RESOLUTION * V_DIVIDER_RATIO)

/* 电流采样：ACS712-5A，2.5V±185mV/A */
#define I_SENSOR_OFFSET_V       2.5f
#define I_SENSOR_GAIN_VA        0.185f
#define I_SCALE                 ((ADC_VREF_V / (float)ADC_RESOLUTION) / I_SENSOR_GAIN_VA)

/* ============================================================
 *  7. 保护
 * ============================================================ */
#define VOUT_OVP_THRESHOLD      30.0f          /* 输出过压 RMS */
#define IOUT_OCP_THRESHOLD      3.0f           /* 输出过流 RMS */
#define IOUT_OC_PEAK_A          5.0f           /* 瞬时过流，硬保护 */
#define VDC_OVP_THRESHOLD       55.0f          /* 母线过压 */
#define TEMP_OTP_THRESHOLD      80.0f          /* 散热片过温 */

#define PROT_CONFIRM_COUNT      3              /* 连续 N 次超限才触发 */
#define PROT_CHECK_PERIOD_MS    50

/* ============================================================
 *  8. 显示
 * ============================================================ */
#define OLED_I2C_ADDR           0x78
#define DISP_PERIOD_MS          200

/* ============================================================
 *  9. 任务模式
 * ============================================================ */
typedef enum {
    TASK_SINGLE = 1,        /* 基本要求：单机逆变 */
    TASK_PARALLEL = 2,      /* 发挥(1)：双机并联 */
    TASK_GRID_TIE = 3,      /* 发挥(2)(3)：并网（电流源模式）*/
} TaskMode;

#define TASK_DEFAULT            TASK_SINGLE

#endif /* __CONFIG_H */
