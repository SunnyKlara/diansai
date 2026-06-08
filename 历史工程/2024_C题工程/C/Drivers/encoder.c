#include "encoder.h"
#include <math.h>
// #include "stm32f1xx_hal.h"

// extern TIM_HandleTypeDef htim2;  // 左轮编码器
// extern TIM_HandleTypeDef htim4;  // 右轮编码器

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

static EncoderData g_encoder_data;

/**
 * @brief 初始化编码器接口
 * @note  TIM2: 左轮编码器 (PA0=CH1, PA1=CH2)
 *        TIM4: 右轮编码器 (PB6=CH1, PB7=CH2)
 *        CubeMX中配置为 Encoder Mode
 */
void Encoder_Init(void)
{
    /*
     * HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
     * HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
     */
    g_encoder_data.left_count = 0;
    g_encoder_data.right_count = 0;
    g_encoder_data.left_speed = 0;
    g_encoder_data.right_speed = 0;
}

/**
 * @brief 读取编码器并计算速度
 * @note  每20ms调用一次（在定时器中断中）
 *        读取计数器值后清零，得到的就是这个周期内的脉冲数
 */
void Encoder_Update(EncoderData *data)
{
    /* 读取计数器值 (有符号，正=正转，负=反转) */
    /*
     * data->left_count  = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
     * data->right_count = (int16_t)__HAL_TIM_GET_COUNTER(&htim4);
     * __HAL_TIM_SET_COUNTER(&htim2, 0);
     * __HAL_TIM_SET_COUNTER(&htim4, 0);
     */
    
    /* 速度 = 脉冲数/采样周期 (脉冲/周期) */
    data->left_speed  = (float)data->left_count;
    data->right_speed = (float)data->right_count;
    
    /* 转换为 m/s */
    /* v = (count / PPR) * π * D / (T/1000) */
    float wheel_circumference = M_PI * WHEEL_DIAMETER_MM / 1000.0f;  // 米
    float sample_period_s = SAMPLE_PERIOD_MS / 1000.0f;
    
    data->left_speed_mps  = ((float)data->left_count / ENCODER_PPR)
                           * wheel_circumference / sample_period_s;
    data->right_speed_mps = ((float)data->right_count / ENCODER_PPR)
                           * wheel_circumference / sample_period_s;
}

EncoderData* Encoder_GetData(void)
{
    return &g_encoder_data;
}

void Encoder_Reset(void)
{
    g_encoder_data.left_count = 0;
    g_encoder_data.right_count = 0;
    g_encoder_data.left_speed = 0;
    g_encoder_data.right_speed = 0;
    g_encoder_data.left_speed_mps = 0;
    g_encoder_data.right_speed_mps = 0;
}
