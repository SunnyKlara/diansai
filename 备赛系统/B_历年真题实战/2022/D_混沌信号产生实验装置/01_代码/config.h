#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        168000000UL
#define DAC_FS_HZ           50000U          /* 50 kSPS x/y 输出           */
#define CHUA_DT             (1.0f / 50000.0f)

/* 蔡氏标准参数（产生双涡卷 Lorenz 形态）*/
#define CHUA_ALPHA          15.6f
#define CHUA_BETA           28.0f
#define CHUA_GAMMA          0.0f
#define CHUA_M0             -1.143f
#define CHUA_M1             -0.714f

/* DAC 输出范围 0~3.3V */
#define DAC_BITS            12
#define DAC_MAX             4095

#endif
