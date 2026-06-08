#include "sensor.h"
// #include "stm32f1xx_hal.h"  // 取消注释以使用实际HAL库

static SensorData g_sensor_data;

/* 传感器权重: 从左到右 */
static const int WEIGHTS[SENSOR_NUM] = {-30, -20, -10, 10, 20, 30};

/**
 * @brief 初始化灰度传感器GPIO
 * @note  默认使用PA0~PA5作为6路灰度传感器输入
 *        根据实际接线修改引脚
 */
void Sensor_Init(void)
{
    /* 
     * 实际使用时取消注释:
     * 
     * GPIO_InitTypeDef GPIO_InitStruct = {0};
     * __HAL_RCC_GPIOA_CLK_ENABLE();
     * 
     * GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
     *                       GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
     * GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
     * GPIO_InitStruct.Pull = GPIO_PULLUP;
     * HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
     */
}

/**
 * @brief 读取所有传感器原始值
 * @note  数字量方式: 1=黑线, 0=白色
 */
void Sensor_ReadAll(SensorData *data)
{
    /*
     * 实际使用时:
     * data->raw[0] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
     * data->raw[1] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
     * data->raw[2] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2);
     * data->raw[3] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3);
     * data->raw[4] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4);
     * data->raw[5] = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5);
     */
}

/**
 * @brief 加权计算偏差值
 * @note  核心循迹算法
 *        偏差 = Σ(weight[i] * sensor[i]) / Σ(sensor[i])
 *        结果为负 -> 偏左, 需要右转
 *        结果为正 -> 偏右, 需要左转
 *        结果为0  -> 在线上, 直行
 * @return 偏差值 (-30.0 ~ +30.0)
 */
float Sensor_CalcPosition(SensorData *data)
{
    int weighted_sum = 0;
    int active_count = 0;
    
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (data->raw[i] == 1) {
            weighted_sum += WEIGHTS[i];
            active_count++;
        }
    }
    
    /* 没有传感器检测到线 -> 丢线处理 */
    if (active_count == 0) {
        /* 保持上次偏差方向，加大修正 */
        if (data->position < 0) {
            data->line_lost = 1;  // 左丢线
            data->position = -30.0f;
        } else {
            data->line_lost = 2;  // 右丢线
            data->position = 30.0f;
        }
        return data->position;
    }
    
    data->line_lost = 0;
    data->position = (float)weighted_sum / (float)active_count;
    
    return data->position;
}

/**
 * @brief 检测特殊路况（十字路口、弯道等）
 */
void Sensor_DetectPattern(SensorData *data)
{
    int active_count = 0;
    int left_active = 0;
    int right_active = 0;
    
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (data->raw[i] == 1) {
            active_count++;
            if (i < SENSOR_NUM / 2) left_active++;
            else right_active++;
        }
    }
    
    /* 十字路口: 大部分传感器都检测到黑线 */
    data->cross_detected = (active_count >= 5) ? 1 : 0;
    
    /* 弯道检测 */
    if (left_active >= 2 && right_active == 0) {
        data->turn_detected = 1;  // 左弯
    } else if (right_active >= 2 && left_active == 0) {
        data->turn_detected = 2;  // 右弯
    } else {
        data->turn_detected = 0;  // 直道
    }
}

/**
 * @brief 获取传感器数据指针
 */
SensorData* Sensor_GetData(void)
{
    return &g_sensor_data;
}
