/**
 * @file    line_drive.h
 * @brief   直线段控制：编码器测距 + IMU 航向闭环
 *
 *  控制结构（外层）：
 *      ┌─ setpoint_yaw（段开始时锁定）
 *      │
 *      ▼
 *      yaw_pid → speed_diff
 *                          \
 *                           ──→ 左轮速度目标 = base + diff
 *                           ──→ 右轮速度目标 = base - diff
 *
 *  距离判停：累计编码器走过 distance_mm 即结束
 *  末段减速：剩余 V_DECEL_DIST_MM 距离时降速到 V_LINE_END_PWM
 */

#ifndef __LINE_DRIVE_H
#define __LINE_DRIVE_H

#include <stdint.h>

typedef enum {
    LINE_RUNNING = 0,
    LINE_DONE,
} LineDriveState;

/**
 * @brief 进入新的直线段
 * @param distance_mm  目标距离
 * @param hold_yaw_deg 锁定的目标航向（一般传入"当前 yaw"）
 */
void LineDrive_Begin(float distance_mm, float hold_yaw_deg);

/**
 * @brief 直线段控制周期（10ms 调用一次）
 *
 * @param yaw_deg          当前 IMU yaw（°，[-180, 180]）
 * @param trip_mm          段开始以来累计行驶距离（mm，左右轮平均）
 * @param[out] tgt_v_left  期望左轮速度（编码器单位：脉冲/周期）
 * @param[out] tgt_v_right 期望右轮速度
 * @return 控制状态
 */
LineDriveState LineDrive_Update(float yaw_deg, float trip_mm,
                                float *tgt_v_left, float *tgt_v_right);

/**
 * @brief 当前段是否走完（外部状态机查询）
 */
uint8_t LineDrive_IsDone(void);

/**
 * @brief 重置（异常停止时调用）
 */
void LineDrive_Reset(void);

#endif /* __LINE_DRIVE_H */
