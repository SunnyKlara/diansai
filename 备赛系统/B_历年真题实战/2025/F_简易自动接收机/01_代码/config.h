#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        168000000UL

#define RX_F_MIN_HZ         1000000U
#define RX_F_MAX_HZ         30000000U
#define RX_IF_HZ            455000U      /* 中频 455kHz                  */

#define SCAN_STEP_HZ        10000U
#define SCAN_DWELL_MS       50

#define AGC_KP              0.5f
#define AGC_TARGET_DBM      -40.0f
#define AGC_MAX_GAIN_DB     60.0f

#define LOCK_RSSI_TH_DBM    -75.0f

#define PI                  3.14159265f
#endif
