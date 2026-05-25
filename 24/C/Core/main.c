/**
 * ============================================================
 *  2024年电赛C题 - 自动行驶小车 主程序
 * ============================================================
 *  主控: STM32F103C8T6
 *  功能: 灰度循迹 + PID控制 + 弯道检测
 * 
 *  控制架构:
 *  ┌──────────┐    ┌──────────┐    ┌──────────┐
 *  │ 灰度传感器 │───>│ 方向PID  │───>│ 差速输出  │
 *  └──────────┘    └──────────┘    └──────────┘
 *                                       │
 *  ┌──────────┐    ┌──────────┐         │
 *  │  编码器   │───>│ 速度PID  │─────────┘
 *  └──────────┘    └──────────┘
 * ============================================================
 */

#include "main.h"
#include "sensor.h"
#include "motor.h"
#include "encoder.h"
#include "pid.h"
#include "track.h"

// #include "stm32f1xx_hal.h"

/* ============ 全局变量 ============ */
static volatile uint8_t control_flag = 0;  // 控制周期标志

/* ============ 函数声明 ============ */
static void System_Init(void);
static void Key_Scan(void);

/**
 * @brief 主函数
 */
int main(void)
{
    /* 系统初始化 */
    System_Init();
    
    /* 等待按键启动 */
    // 可以用按键或者延时启动
    // while (HAL_GPIO_ReadPin(KEY_START_PORT, KEY_START_PIN) == GPIO_PIN_SET);
    // HAL_Delay(500);  // 消抖
    
    /* 启动循迹 */
    Track_Start();
    
    /* ============ 主循环 ============ */
    while (1)
    {
        /* 等待控制周期标志 (由定时器中断置位) */
        if (control_flag)
        {
            control_flag = 0;
            
            /* 核心: 循迹控制 */
            Track_Control();
            
            /* 按键检测 (停车) */
            Key_Scan();
        }
        
        /* 
         * 可以在这里做一些非实时任务:
         * - OLED显示传感器状态
         * - 串口打印调试信息
         * - 蓝牙遥控参数调整
         */
    }
}

/**
 * @brief 系统初始化
 */
static void System_Init(void)
{
    /*
     * HAL_Init();
     * SystemClock_Config();  // 72MHz
     */
    
    /* 外设初始化 */
    Sensor_Init();
    Motor_Init();
    Encoder_Init();
    Track_Init();
    
    /* 
     * 启动控制定时器 (TIM1, 20ms中断)
     * HAL_TIM_Base_Start_IT(&htim1);
     */
}

/**
 * @brief 按键扫描
 */
static void Key_Scan(void)
{
    /*
     * static uint8_t key_state = 0;
     * if (HAL_GPIO_ReadPin(KEY_START_PORT, KEY_START_PIN) == GPIO_PIN_RESET) {
     *     HAL_Delay(20);  // 消抖
     *     if (HAL_GPIO_ReadPin(KEY_START_PORT, KEY_START_PIN) == GPIO_PIN_RESET) {
     *         if (Track_GetState() == CAR_RUNNING || 
     *             Track_GetState() == CAR_TURNING) {
     *             Track_Stop();
     *         } else {
     *             Track_Start();
     *         }
     *         while (HAL_GPIO_ReadPin(KEY_START_PORT, KEY_START_PIN) == GPIO_PIN_RESET);
     *     }
     * }
     */
}

/**
 * @brief 定时器中断回调 (20ms)
 * @note  在stm32f1xx_it.c中会调用此函数
 */
/*
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        control_flag = 1;
        
        // 同时更新编码器数据
        EncoderData *enc = Encoder_GetData();
        Encoder_Update(enc);
    }
}
*/
