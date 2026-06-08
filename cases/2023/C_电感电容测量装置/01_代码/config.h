/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        72000000UL    /* STM32F103 主频              */
#define DDS_FREQ_HZ         10000U        /* 测试激励 10kHz              */
#define ADC_FS_HZ           200000U       /* 200kSPS                     */

/* 测量量程 */
#define L_MIN_UH            10.0f
#define L_MAX_UH            10000.0f
#define C_MIN_PF            10.0f
#define C_MAX_NF            100.0f

/* 校准基准电阻 */
#define R_REF_OHM           1000.0f

#define V_PER_LSB           0.00081f
#define ADC_OFFSET_LSB      2048.0f

#define PI                  3.14159265f
#define TWO_PI              6.28318531f

#endif
