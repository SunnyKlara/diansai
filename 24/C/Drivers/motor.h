#ifndef __MOTOR_H
#define __MOTOR_H

#include <stdint.h>

/* PWM最大值 (对应定时器ARR) */
#define MOTOR_PWM_MAX   999

/* 电机方向 */
typedef enum {
    MOTOR_FORWARD = 0,
    MOTOR_BACKWARD,
    MOTOR_STOP
} MotorDir;

/**
 * @brief 初始化电机驱动
 * @note  配置PWM输出和方向控制GPIO
 *        左电机: TIM3_CH3 (PWM) + PB12/PB13 (方向)
 *        右电机: TIM3_CH4 (PWM) + PB14/PB15 (方向)
 */
void Motor_Init(void);

/**
 * @brief 设置左电机速度
 * @param speed: -999 ~ +999, 正=前进, 负=后退
 */
void Motor_SetLeft(int16_t speed);

/**
 * @brief 设置右电机速度
 * @param speed: -999 ~ +999, 正=前进, 负=后退
 */
void Motor_SetRight(int16_t speed);

/**
 * @brief 同时设置两个电机
 * @param left_speed:  左轮速度
 * @param right_speed: 右轮速度
 */
void Motor_SetBoth(int16_t left_speed, int16_t right_speed);

/**
 * @brief 紧急停车
 */
void Motor_Stop(void);

#endif /* __MOTOR_H */
