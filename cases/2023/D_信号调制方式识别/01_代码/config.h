#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ          168000000UL
#define MOD_FRAME_LEN         1024U
#define MOD_FS_HZ             200000U     /* 200 kSPS                    */

/* 决策阈值（实测调）*/
#define TH_SIGMA_A_AMPLITUDE  0.30f
#define TH_SIGMA_A_2ASK       0.60f
#define TH_SIGMA_F_FM         0.20f
#define TH_SIGMA_F_2FSK       0.50f

#define PI                    3.14159265f
#define TWO_PI                6.28318531f

#endif
