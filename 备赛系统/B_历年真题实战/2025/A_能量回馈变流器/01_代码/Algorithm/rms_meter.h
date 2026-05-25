/**
 * @file    rms_meter.h
 * @brief   滑动窗口 RMS 计算（电压 / 电流）
 *
 *  原理：每个周期累积 sum(x²)，N 个采样点后求 sqrt(sum/N)。
 *  适用频率范围：20~100Hz，对应基波周期 50ms~10ms。
 *  采样率 20kHz → 一个 50Hz 周期 400 个点。
 *
 *  实现：固定 N = 512（覆盖最低频率 20Hz / 25 周期 = 1.25ms 余量），
 *        滑动窗口（每次 sample 进来，移除最老的点，加入新点）。
 */

#ifndef __RMS_METER_H
#define __RMS_METER_H

#include <stdint.h>
#include "../config.h"

typedef struct {
    float sample_sq[RMS_BUFFER_LEN];   /* 平方值滑动窗口 */
    uint16_t idx;                      /* 写入位置 */
    float sum_sq;                      /* 平方和（增量更新）*/
    float rms;                         /* 最新 RMS 值 */
    uint8_t ready;                     /* 0=尚未填满一个完整窗口 */
    uint16_t fill_count;
} RMSMeter;

/**
 * @brief 初始化 RMS 计量器
 */
void RMS_Init(RMSMeter *m);

/**
 * @brief 喂入一个新采样（已减去直流偏置 + 转换成实际物理量）
 * @return 当前 RMS 值
 */
float RMS_Update(RMSMeter *m, float sample);

/**
 * @brief 获取最近 RMS（不更新）
 */
float RMS_Get(const RMSMeter *m);

/**
 * @brief 重置（启动 / 故障恢复时调用）
 */
void RMS_Reset(RMSMeter *m);

#endif /* __RMS_METER_H */
