/*
 * tof.h  -  VL53L0X ToF laser height sensor glue (continuous, non-blocking)
 *
 * Drop-in replacement for the ultrasonic height path. The control layer only
 * ever reads current_height / height_updated (see global.h), so selecting this
 * source via HEIGHT_SENSOR_TOF in config.h changes nothing downstream.
 *
 * Tof_Init()    : one-shot bring-up (I2C + DataInit/StaticInit/Ref cal + mode +
 *                 start continuous ranging). Call once from USER CODE 2.
 * Tof_Measure() : non-blocking, call every main loop pass. Polls data-ready;
 *                 on a fresh valid sample it updates current_height and sets
 *                 height_updated (same event the loop already consumes).
 */
#ifndef __TOF_H
#define __TOF_H

#include "stm32h7xx_hal.h"

uint8_t Tof_Init(void);     /* 0 = ok, non-zero = init failed (see g_tof_status) */
void    Tof_Measure(void);

/* telemetry (mirrors the ultrasonic g_ultra_raw for the serial heartbeat) */
extern volatile float    g_tof_raw_mm;   /* last raw range (mm), pre-geometry */
extern volatile uint8_t  g_tof_status;   /* last VL53L0X RangeStatus */
extern volatile uint8_t  g_tof_id;       /* model id read at init (expect 0xEE) - I2C alive test */
extern volatile uint8_t  g_tof_init;     /* init result: 0=ok, else step index that failed (1..9) */

#endif /* __TOF_H */
