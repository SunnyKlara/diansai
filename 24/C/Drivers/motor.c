#include "motor.h"
// #include "stm32f1xx_hal.h"  // 取消注释以使用实际HAL库

// extern TIM_HandleTypeDef htim3;  // PWM定时器

/**
 * @brief 初始化电机驱动
 * @note  TB6612接线方案:
 *        PWMA -> TIM3_CH3 (PB0)   左电机PWM
 *        PWMB -> TIM3_CH4 (PB1)   右电机PWM
 *        AIN1 -> PB12              左电机方向1
 *        AIN2 -> PB13              左电机方向2
 *        BIN1 -> PB14              右电机方向1
 *        BIN2 -> PB15              右电机方向2
 *        STBY -> 3.3V (常使能)
 */
void Motor_Init(void)
{
    /*
     * 实际使用时:
     * 1. 在CubeMX中配置TIM3 CH3/CH4为PWM输出
     * 2. 配置PB12~PB15为推挽输出
     * 3. 启动PWM:
     *    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
     *    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
     */
}

/**
 * @brief 设置左电机速度和方向
 */
void Motor_SetLeft(int16_t speed)
{
    /* 限幅 */
    if (speed > MOTOR_PWM_MAX) speed = MOTOR_PWM_MAX;
    if (speed < -MOTOR_PWM_MAX) speed = -MOTOR_PWM_MAX;
    
    if (speed >= 0) {
        /* 正转 */
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        // __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, speed);
    } else {
        /* 反转 */
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
        // __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, -speed);
    }
}

/**
 * @brief 设置右电机速度和方向
 */
void Motor_SetRight(int16_t speed)
{
    if (speed > MOTOR_PWM_MAX) speed = MOTOR_PWM_MAX;
    if (speed < -MOTOR_PWM_MAX) speed = -MOTOR_PWM_MAX;
    
    if (speed >= 0) {
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
        // __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, speed);
    } else {
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
        // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
        // __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, -speed);
    }
}

/**
 * @brief 同时设置两个电机
 */
void Motor_SetBoth(int16_t left_speed, int16_t right_speed)
{
    Motor_SetLeft(left_speed);
    Motor_SetRight(right_speed);
}

/**
 * @brief 紧急停车
 */
void Motor_Stop(void)
{
    Motor_SetBoth(0, 0);
    // 也可以拉低所有方向引脚实现刹车
    // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12 | GPIO_PIN_13, GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_RESET);
}
