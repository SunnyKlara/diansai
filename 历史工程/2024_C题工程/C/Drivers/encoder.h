#ifndef __ENCODER_H
#define __ENCODER_H

#include <stdint.h>

/**
 * @brief 编码器数据结构
 * @note  用于测量电机实际转速，实现速度闭环
 */
typedef struct {
    int32_t  left_count;     // 左轮编码器计数
    int32_t  right_count;    // 右轮编码器计数
    float    left_speed;     // 左轮速度 (脉冲/采样周期)
    float    right_speed;    // 右轮速度
    float    left_speed_mps; // 左轮速度 (m/s)
    float    right_speed_mps;// 右轮速度 (m/s)
} EncoderData;

/* 编码器参数 (根据实际电机修改) */
#define ENCODER_PPR         330     // 编码器每圈脉冲数 (4倍频后)
#define WHEEL_DIAMETER_MM   65      // 轮子直径 (mm)
#define SAMPLE_PERIOD_MS    20      // 采样周期 (ms)

/**
 * @brief 初始化编码器
 * @note  使用TIM2(左轮)和TIM4(右轮)的编码器模式
 */
void Encoder_Init(void);

/**
 * @brief 读取编码器计数并计算速度
 * @note  在定时器中断中周期调用 (20ms)
 */
void Encoder_Update(EncoderData *data);

/**
 * @brief 获取编码器数据
 */
EncoderData* Encoder_GetData(void);

/**
 * @brief 重置编码器计数
 */
void Encoder_Reset(void);

#endif /* __ENCODER_H */
