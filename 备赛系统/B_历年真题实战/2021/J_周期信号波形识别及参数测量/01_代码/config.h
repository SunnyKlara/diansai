/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        168000000UL
#define ADC_FS_HIGH_HZ      500000U   /* 高频档：50kHz 信号 → 500kSPS  */
#define ADC_FS_MID_HZ       100000U   /* 中频档                          */
#define ADC_FS_LOW_HZ       2000U     /* 低频档：1Hz 信号 → 2kSPS       */

#define FFT_SIZE            1024U

#define V_PER_LSB           0.00081f  /* 3.3V / 4096                    */
#define ADC_OFFSET_LSB      2048.0f

/* 量程档位（CD4053 控制）*/
typedef enum { RNG_LOW = 0, RNG_MID = 1, RNG_HIGH = 2 } range_t;
#define RNG_LOW_GAIN        10.0f     /* 50mV~200mV 输入 ×10           */
#define RNG_MID_GAIN        1.0f
#define RNG_HIGH_GAIN       0.25f     /* 2~10V 输入 ÷4                  */

/* 数学 */
#define PI                  3.14159265f
#define TWO_PI              6.28318531f

#endif
