/**
 * @file    path.h
 * @brief   任务路径管理：把比赛要求的 4 个任务拆成"段序列"
 *
 *  设计思路：
 *      把场地行驶抽象成一系列"段"，每段是直线 / 弧线 / 原地转向。
 *      状态机消费段，段消费完任务结束。
 *
 *  3 种段类型：
 *      SEG_TURN  - 原地转向，目标 yaw 偏移
 *      SEG_LINE  - 直线，目标距离 + 保持当前航向
 *      SEG_ARC   - 弧线，红外循迹，弧长达预期且全白时结束
 */

#ifndef __PATH_H
#define __PATH_H

#include <stdint.h>

typedef enum {
    SEG_END  = 0,    /* 任务结束哨兵 */
    SEG_TURN,
    SEG_LINE,
    SEG_ARC,
} SegType;

typedef struct {
    SegType type;
    float   distance_mm;     /* SEG_LINE/SEG_ARC：目标距离（mm）；SEG_TURN：忽略 */
    float   delta_yaw_deg;   /* SEG_TURN：目标转角（°，正=逆时针）；其他段忽略 */
    uint8_t arc_dir;         /* SEG_ARC：0=循迹方向无偏好，1=偏左，2=偏右（用于交叉路径外侧弧）*/
} Segment;

/**
 * @brief 装载某个任务的段序列
 * @param task_id 任务编号 1/2/3/4
 */
void Path_Load(uint8_t task_id);

/**
 * @brief 当前段
 */
const Segment* Path_Current(void);

/**
 * @brief 推进到下一段
 * @return 1 = 还有下一段，0 = 任务结束
 */
uint8_t Path_Next(void);

/**
 * @brief 重置到第一段
 */
void Path_Reset(void);

/**
 * @brief 当前段索引（从 0 开始）
 */
uint16_t Path_Index(void);

/**
 * @brief 任务总段数（不含 SEG_END）
 */
uint16_t Path_TotalSegments(void);

#endif /* __PATH_H */
