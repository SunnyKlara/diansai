/**
 * 2025E 瞄准模块控制 - STM32F103C8T6 (或其他非MSPM0的MCU)
 * 
 * 功能：控制二维云台使蓝紫激光笔指向目标靶
 * 传感器：OpenMV摄像头（检测靶标位置）或 IMU+编码器（运动补偿）
 * 执行器：舵机×2（俯仰+偏航）
 * 
 * 引脚:
 *   PA1  -> 偏航舵机PWM (TIM2_CH2)
 *   PA2  -> 俯仰舵机PWM (TIM2_CH3)
 *   PA9  -> UART1_TX (接OpenMV RX)
 *   PA10 -> UART1_RX (接OpenMV TX)
 *   PB0  -> 激光笔开关 (GPIO)
 *   PC13 -> 启动按键
 */

#include "stm32f1xx_hal.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ========== 参数定义 ========== */
// 舵机角度范围
#define YAW_CENTER    1500   // 偏航中位 (μs)
#define PITCH_CENTER  1500   // 俯仰中位 (μs)
#define SERVO_MIN     500    // 舵机最小脉宽 (μs)
#define SERVO_MAX     2500   // 舵机最大脉宽 (μs)

// 靶标参数
#define TARGET_DIST_MM  500  // 靶标距离约500mm (轨迹外50cm)

// PID参数 - 偏航
#define KP_YAW   3.0f
#define KI_YAW   0.1f
#define KD_YAW   1.5f

// PID参数 - 俯仰
#define KP_PITCH 3.0f
#define KI_PITCH 0.1f
#define KD_PITCH 1.5f

/* ========== 全局变量 ========== */
// OpenMV发送的靶标位置 (像素坐标)
volatile int16_t target_cx = 0;  // 靶心X (图像中心=0)
volatile int16_t target_cy = 0;  // 靶心Y
volatile uint8_t target_found = 0;

// 舵机当前脉宽
static uint16_t yaw_us = YAW_CENTER;
static uint16_t pitch_us = PITCH_CENTER;

/* ========== 舵机控制 ========== */
void servo_set_yaw(uint16_t us) {
    if (us < SERVO_MIN) us = SERVO_MIN;
    if (us > SERVO_MAX) us = SERVO_MAX;
    yaw_us = us;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, us);
}

void servo_set_pitch(uint16_t us) {
    if (us < SERVO_MIN) us = SERVO_MIN;
    if (us > SERVO_MAX) us = SERVO_MAX;
    pitch_us = us;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, us);
}

/* ========== 激光笔控制 ========== */
void laser_on(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
}

void laser_off(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
}

/* ========== OpenMV通信 ========== */
// OpenMV端发送格式: "$cx,cy,found\n"
// 例: "$32,-15,1\n" 表示靶心在图像中心偏右32像素、偏上15像素

static char uart_buf[64];
static uint8_t uart_idx = 0;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    static uint8_t rx_byte;
    
    if (huart->Instance == USART1) {
        if (rx_byte == '\n') {
            uart_buf[uart_idx] = '\0';
            // 解析数据
            if (uart_buf[0] == '$') {
                int cx, cy, found;
                if (sscanf(uart_buf, "$%d,%d,%d", &cx, &cy, &found) == 3) {
                    target_cx = cx;
                    target_cy = cy;
                    target_found = found;
                }
            }
            uart_idx = 0;
        } else {
            if (uart_idx < sizeof(uart_buf) - 1) {
                uart_buf[uart_idx++] = rx_byte;
            }
        }
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}

/* ========== PID控制器 ========== */
typedef struct {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float output_max;
} PID_t;

float pid_compute(PID_t* pid, float error) {
    pid->integral += error;
    if (pid->integral > pid->output_max) pid->integral = pid->output_max;
    if (pid->integral < -pid->output_max) pid->integral = -pid->output_max;
    
    float deriv = error - pid->prev_error;
    pid->prev_error = error;
    
    float out = pid->kp * error + pid->ki * pid->integral + pid->kd * deriv;
    if (out > pid->output_max) out = pid->output_max;
    if (out < -pid->output_max) out = -pid->output_max;
    
    return out;
}

/* ========== 主程序 ========== */
PID_t pid_yaw   = {KP_YAW,   KI_YAW,   KD_YAW,   0, 0, 300};
PID_t pid_pitch  = {KP_PITCH, KI_PITCH, KD_PITCH, 0, 0, 300};

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM2_Init();    // 舵机PWM (50Hz)
    MX_USART1_Init();  // OpenMV通信
    
    // 舵机归中
    servo_set_yaw(YAW_CENTER);
    servo_set_pitch(PITCH_CENTER);
    
    // 启动串口接收
    uint8_t rx_byte;
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    
    // 启动舵机PWM
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    
    laser_off();
    
    // 等待启动按键
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        HAL_Delay(10);
    }
    HAL_Delay(200);
    
    laser_on();
    
    /* 瞄准主循环 (20ms周期) */
    while (1) {
        if (target_found) {
            // 像素偏差 → 舵机调整量
            // OpenMV图像中心为(0,0)，偏差单位为像素
            // 需要根据实际标定确定像素到角度的映射关系
            
            float yaw_correction = pid_compute(&pid_yaw, (float)target_cx);
            float pitch_correction = pid_compute(&pid_pitch, (float)target_cy);
            
            // 更新舵机位置
            servo_set_yaw(yaw_us + (int16_t)yaw_correction);
            servo_set_pitch(pitch_us - (int16_t)pitch_correction);  // 注意方向
        }
        
        HAL_Delay(20);  // 50Hz控制频率
    }
}
