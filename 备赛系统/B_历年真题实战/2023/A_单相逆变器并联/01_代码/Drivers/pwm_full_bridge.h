/**
 * @file    pwm_full_bridge.h
 * @brief   单相全桥 PWM 封装（TIM1 双通道 + 互补 + 死区）
 *
 *  封装 main.c 中裸写 TIM1 寄存器的部分，让算法层不再依赖 stm32g4xx_hal.h。
 *
 *  典型用法：
 *      PWMFB_Init();
 *      PWMFB_Start();
 *      在 TIM1 中断里：PWMFB_SetDuty(duty_a, duty_b);
 *      故障时：PWMFB_EmergencyOff();
 */

#ifndef __PWM_FULL_BRIDGE_H
#define __PWM_FULL_BRIDGE_H

#include <stdint.h>

void PWMFB_Init(void);

/** @brief 启动 PWM 输出（4 路：CH1/CH1N + CH2/CH2N） */
void PWMFB_Start(void);

/** @brief 停止 PWM（GPIO 拉低，安全态） */
void PWMFB_Stop(void);

/**
 * @brief 设置两个桥臂的占空比
 * @param duty_a  A 桥臂占空比 [0.0, 1.0]
 * @param duty_b  B 桥臂占空比 [0.0, 1.0]
 *
 *  双极性 SPWM 中，duty_b = 1 - duty_a。
 *  单极性可独立设置。
 */
void PWMFB_SetDuty(float duty_a, float duty_b);

/** @brief 紧急关断（中断中可调用，写 BDTR.MOE = 0）*/
void PWMFB_EmergencyOff(void);

#endif /* __PWM_FULL_BRIDGE_H */
