/**
 * @file    motor.h
 * @brief   双电机驱动（TB6612）
 */

#ifndef __MOTOR_H
#define __MOTOR_H

#include <stdint.h>

void Motor_Init(void);

/**
 * @brief 设置左电机 PWM
 * @param pwm  -999~999，正=前进，负=后退
 *             ⚠️ 题目要求只能前进，业务层应保证 pwm >= 0；
 *                负值仅供调试用（让车快速回起点）
 */
void Motor_SetLeft(int16_t pwm);

void Motor_SetRight(int16_t pwm);

/** @brief 同时设置左右 */
void Motor_SetBoth(int16_t left_pwm, int16_t right_pwm);

/** @brief 立即停止（PWM=0 + 方向引脚拉低 → 滑停） */
void Motor_Stop(void);

/** @brief 紧急刹车（短路两端 → 电气制动）*/
void Motor_Brake(void);

#endif /* __MOTOR_H */
