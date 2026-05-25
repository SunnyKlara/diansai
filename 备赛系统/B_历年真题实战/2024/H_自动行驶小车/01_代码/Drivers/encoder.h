/**
 * @file    encoder.h
 * @brief   左右轮编码器（MSPM0 TIMG 编码器模式）
 *
 *  采样模型：
 *      硬件计数器持续累积；上层每 10ms 调一次 Encoder_Sample()，
 *      读取增量并清零，得到本周期的脉冲数。
 *      速度 = 脉冲数 / 周期；距离累积由上层维护（mm = 脉冲 × MM_PER_PULSE）。
 */

#ifndef __ENCODER_H
#define __ENCODER_H

#include <stdint.h>

typedef struct {
    int32_t left_delta;       /* 本周期左轮脉冲（带符号）*/
    int32_t right_delta;      /* 本周期右轮脉冲 */
    float   left_speed_pps;   /* 左轮速度（脉冲/周期）*/
    float   right_speed_pps;  /* 右轮速度 */
    float   trip_left_mm;     /* 段累计左轮距离 */
    float   trip_right_mm;    /* 段累计右轮距离 */
} EncoderData;

void Encoder_Init(void);

/**
 * @brief 周期采样（10ms 调用）
 */
void Encoder_Sample(EncoderData *out);

/**
 * @brief 段开始时清零累计距离（不影响速度采样）
 */
void Encoder_ResetTrip(void);

/**
 * @brief 获取段累计平均距离（mm）
 */
float Encoder_TripMM(void);

#endif /* __ENCODER_H */
