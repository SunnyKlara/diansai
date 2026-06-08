/**
 * @file    track.h
 * @brief   弧线段循迹控制（差速 PID）
 *
 *  与直线段的区别：直线段靠 IMU 保持航向；弧线段靠红外加权位置作为偏差。
 *  退出条件：编码器走过预期弧长 90%+ AND 连续若干周期所有传感器都白。
 */

#ifndef __TRACK_H
#define __TRACK_H

#include <stdint.h>

typedef enum {
    TRACK_RUNNING = 0,
    TRACK_DONE,
    TRACK_LOST,        /* 长时间丢线 */
} TrackState;

/**
 * @brief 进入新的弧线段
 * @param expect_arc_mm  预期弧长（用于退出判定）
 * @param dir_hint       0/1/2，循迹偏好（外侧弧时使用）
 */
void Track_Begin(float expect_arc_mm, uint8_t dir_hint);

/**
 * @brief 弧线段控制（20ms 调用）
 *
 * @param ir_raw           5 路红外原始值（1=黑线，0=白）
 * @param trip_mm          段开始以来累计弧长
 * @param[out] tgt_v_left  期望左轮速度
 * @param[out] tgt_v_right 期望右轮速度
 */
TrackState Track_Update(const uint8_t ir_raw[5], float trip_mm,
                        float *tgt_v_left, float *tgt_v_right);

uint8_t Track_IsDone(void);
void    Track_Reset(void);

/**
 * @brief 计算红外加权位置（仅工具，单元测试用）
 * @return 加权位置 [-2.0, 2.0]，无传感器压线返回 NaN（实际用 -10.0 表示）
 */
float   Track_CalcPosition(const uint8_t ir_raw[5]);

#endif /* __TRACK_H */
