#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ          168000000UL
#define TDR_PULSE_NS          10U
#define TDR_FS_HZ             1000000U   /* 1MSPS                       */
#define TDR_EQUIV_OVERSAMPLE  100U
#define TDR_EQUIV_FS_HZ       (TDR_FS_HZ * TDR_EQUIV_OVERSAMPLE)  /* 100MSPS */
#define TDR_FRAME_LEN         1000U      /* 1000 等效点 → 10μs            */

#define VP_MPS                2.0e8f     /* 2c/3，RG58                   */
#define Z0_OHM                50.0f

#define V_PER_LSB             0.00081f
#define ADC_OFFSET_LSB        2048.0f

#define PI                    3.14159265f
#endif
