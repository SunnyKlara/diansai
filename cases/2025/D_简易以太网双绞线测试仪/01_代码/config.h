#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ          168000000UL
#define TP_NUM_PAIRS          4U
#define TP_FRAME_LEN          1000U
#define TP_LENGTH_MIN_M       0.5f
#define TP_LENGTH_MAX_M       100.0f

/* 双绞线 Cat5e Vp ≈ 0.65c */
#define VP_MPS                1.95e8f
#define Z0_OHM                100.0f       /* 双绞线特性阻抗 100Ω        */

#define V_PER_LSB             0.00081f
#define ADC_OFFSET_LSB        2048.0f

#endif
