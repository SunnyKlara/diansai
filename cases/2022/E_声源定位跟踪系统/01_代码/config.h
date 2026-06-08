#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        168000000UL
#define MIC_NUM             4
#define MIC_FS_HZ           96000U
#define TDOA_FRAME_LEN      4096U

#define SOUND_SPEED         343.0f          /* m/s @ 20°C                 */
#define MIC_DIST_M          0.20f           /* 阵列对角线 20cm            */

#define SERVO_KP            0.6f
#define SERVO_TOL_DEG       2.0f

#endif
