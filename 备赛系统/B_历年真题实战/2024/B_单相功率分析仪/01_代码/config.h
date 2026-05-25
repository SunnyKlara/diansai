/**
 * @file    config.h
 * @brief   2024 B 单相功率分析仪 全局可调参数
 * @note    所有需要调试时修改的参数集中在这里。
 *          严禁在 .c 文件里硬编码"魔法数字"。
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>

/* ============================================================
 *  1. 基波 / 采样参数
 * ============================================================ */
#define FUND_FREQ_HZ          50.0f     /* 工频 50 Hz */
#define CYCLES_PER_MEAS       5         /* 每次测量累积 5 个周期 */
#define SAMPLES_PER_CYCLE     200       /* 每周期 200 点 */
#define DFT_N                 (CYCLES_PER_MEAS * SAMPLES_PER_CYCLE)
                                        /* 1000 点（不是 1024，整周期消除泄漏）*/
#define SAMPLE_RATE_HZ        ((float)SAMPLES_PER_CYCLE * FUND_FREQ_HZ)
                                        /* = 10000 Hz */

/* 谐波分析次数 */
#define NUM_HARMONICS         10

/* 基波在 DFT 中的 bin 索引（精确整数，因为整周期采样）*/
#define FUND_BIN              (CYCLES_PER_MEAS)
                                        /* 50Hz × 1000 / 10000 = 5 */

/* ============================================================
 *  2. ADC（MSP430FR5994 ADC12_B）
 * ============================================================ */
#define ADC_RESOLUTION        4096U     /* 12bit */
#define ADC_VREF_V            3.3f
#define ADC_OFFSET_RAW        2048U     /* 1.65V 偏置点 */

/* ADC 通道映射（CubeMX/SimpleLink 配置后填具体引脚）*/
#define ADC_CH_VOLTAGE        0          /* A0 = P1.0，电压采样 */
#define ADC_CH_CURRENT        1          /* A1 = P1.1，电流采样 */

/* ============================================================
 *  3. 互感器变比（出厂校准存 FRAM）
 * ============================================================ */
/* 电压：ZMPT101B 1:1 互感器 + 100k/1k 副边分压
 * 实际 V = adc_v * V_GAIN + V_OFFSET
 * V_GAIN 由校准得出，初值用理论值
 */
#define V_GAIN_INIT           ((ADC_VREF_V / (float)ADC_RESOLUTION) * 100.0f)
                                        /* 理论 ≈ 0.0806 V/LSB */
#define V_OFFSET_INIT         0.0f      /* 校准时含直流偏置补偿 */

/* 电流：ZMCT103C，1000:1 变比 + 100Ω 采样电阻 + N 匝穿心
 * 实际 I = adc_i * I_GAIN + I_OFFSET
 */
#define I_TURNS               10        /* 穿心匝数（提高灵敏度）*/
#define I_BURDEN_OHM          100.0f
#define I_RATIO               1000.0f
#define I_GAIN_INIT           ((ADC_VREF_V / (float)ADC_RESOLUTION) \
                              / (I_BURDEN_OHM / I_RATIO * (float)I_TURNS))
                                        /* 理论 ≈ 8.06e-4 A/LSB */
#define I_OFFSET_INIT         0.0f

/* ============================================================
 *  4. 显示（HT1621 段码 LCD）
 * ============================================================ */
#define DISP_ROTATE_PERIOD_S  5         /* 多屏轮显，5s 切一次 */

/* 显示页 */
typedef enum {
    DISP_PAGE_VRMS_IRMS = 0,
    DISP_PAGE_P_PF,
    DISP_PAGE_THD_H1_H2,
    DISP_PAGE_H3_H4_H5,
    DISP_PAGE_H6_H7_H8,
    DISP_PAGE_H9_H10_TOTAL,
    DISP_PAGE_COUNT
} DispPage;

/* ============================================================
 *  5. 系统时序
 * ============================================================ */
#define MEAS_PERIOD_MS        1000      /* 1 秒一次完整测量 */

/* ============================================================
 *  6. 低功耗
 * ============================================================ */
/* MCU 主频。降频可进一步省电（8 MHz 足够运行 DFT）*/
#define MCU_FREQ_HZ           16000000UL

/* ============================================================
 *  7. 校准
 * ============================================================ */
#define CALIB_MAGIC           0xCA11A6E0UL    /* FRAM 校准数据有效标记 */

#endif /* __CONFIG_H */
