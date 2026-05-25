/**
 * @file    pwm_3phase.h
 * @brief   三相互补 PWM 启停封装（TIM1 + 死区）
 */

#ifndef __PWM_3PHASE_H
#define __PWM_3PHASE_H

#include <stdint.h>

void PWM3Phase_Init(void);

/**
 * @brief 启动三相互补 PWM 输出
 */
void PWM3Phase_Start(void);

/**
 * @brief 停止三相互补 PWM 输出（GPIO 拉低，安全态）
 */
void PWM3Phase_Stop(void);

/**
 * @brief 直接写三相比较寄存器（占空比）
 * @param Ta, Tb, Tc  归一化占空比 [0, 1]
 */
void PWM3Phase_SetDuty(float Ta, float Tb, float Tc);

/**
 * @brief 紧急关断（中断中可调用，写 BDTR.MOE=0）
 */
void PWM3Phase_EmergencyOff(void);

#endif /* __PWM_3PHASE_H */
