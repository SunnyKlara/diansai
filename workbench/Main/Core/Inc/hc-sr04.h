/*
 * @Author       : yzy
 * @Date         : 2021-05-31 17:03:27
 * @LastEditors  : yzy
 * @LastEditTime : 2021-05-31 19:02:54
 * @Description  : 
 * @FilePath     : \F103_Test\BSP_HARDWARE\hc-sr04.h
 */
#ifndef HCSR04_H_
#define HCSR04_H_

#include "stm32h7xx_hal.h"

void Ultrasonic_Trigger(void);
float Ultrasonic_GetDistance(void);
void Ultrasonic_HandleTimeout(void);
void Ultrasonic_Measure(void);
void Ultrasonic_ResetState(void);   // 重置传感器验证状态（模式切换时调用）

/* Fan tachometer (register-level TIM3_CH1 input capture on PC6, AF2).
 * Self-contained in hc-sr04.c: does not touch tim.c / it.c / Keil project.
 * Call Tach_Init() once from USER CODE 2; Fan_GetRPM() returns latest RPM
 * (0 when stopped, i.e. no pulse within TACH_TIMEOUT_MS). */
void     Tach_Init(void);
uint32_t Fan_GetRPM(void);

extern volatile float g_ultra_raw;   /* pre-filter raw echo distance (cm), telemetry */
extern volatile float g_max_jump;    /* sensor jump-reject threshold (cm), runtime-tunable */
extern volatile uint32_t g_ultra_trig_ms; /* ultrasonic trigger period (ms), runtime-tunable ('p') */
#endif /* HCSR04_H_ */


