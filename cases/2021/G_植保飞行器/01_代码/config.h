#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        72000000UL

#define SPRAY_DURATION_MS   500U
#define SPRAY_FLOW_ML_PER_MIN  80
#define MISSION_AREA_M2     20.0f

#define LORA_FREQ_MHZ       433
#define LORA_BAUD           9600

/* 视觉识别目标 */
#define TARGET_COLOR_GREEN  1
#define TARGET_COLOR_RED    2

#endif
