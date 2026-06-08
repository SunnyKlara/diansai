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

#include "main.h"
#include "oled.h"
#include "tim.h"
void Ultrasonic_Trigger(void);
float Ultrasonic_GetDistance(void);
void Ultrasonic_HandleTimeout(void);
void chaoshenbo(void);
#endif /* HCSR04_H_ */


