#ifndef __MAIN_H
#define __MAIN_H

/* ============ 系统参数 ============ */
#define CONTROL_PERIOD_MS   20    // 控制周期 20ms (50Hz)

/* ============ 引脚定义 (根据实际接线修改) ============ */

/* 灰度传感器 PA0~PA5 */
#define SENSOR_PORT         GPIOA
#define SENSOR_PIN_0        GPIO_PIN_0
#define SENSOR_PIN_1        GPIO_PIN_1
#define SENSOR_PIN_2        GPIO_PIN_2
#define SENSOR_PIN_3        GPIO_PIN_3
#define SENSOR_PIN_4        GPIO_PIN_4
#define SENSOR_PIN_5        GPIO_PIN_5

/* 电机PWM: TIM3 CH3(PB0) CH4(PB1) */
/* 电机方向: PB12~PB15 */
#define MOTOR_L_IN1_PORT    GPIOB
#define MOTOR_L_IN1_PIN     GPIO_PIN_12
#define MOTOR_L_IN2_PORT    GPIOB
#define MOTOR_L_IN2_PIN     GPIO_PIN_13
#define MOTOR_R_IN1_PORT    GPIOB
#define MOTOR_R_IN1_PIN     GPIO_PIN_14
#define MOTOR_R_IN2_PORT    GPIOB
#define MOTOR_R_IN2_PIN     GPIO_PIN_15

/* 编码器: TIM2(PA0,PA1) TIM4(PB6,PB7) */
/* 注意: 如果编码器和灰度传感器都用PA0/PA1会冲突，需要调整 */

/* 按键 (启动/停止) */
#define KEY_START_PORT      GPIOC
#define KEY_START_PIN       GPIO_PIN_13

/* LED指示灯 */
#define LED_PORT            GPIOC
#define LED_PIN             GPIO_PIN_13

#endif /* __MAIN_H */
