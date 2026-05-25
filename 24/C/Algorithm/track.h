#ifndef __TRACK_H
#define __TRACK_H

#include "pid.h"
#include "sensor.h"
#include "encoder.h"

/* 小车运行状态 */
typedef enum {
    CAR_IDLE = 0,       // 待机
    CAR_RUNNING,        // 正常循迹行驶
    CAR_TURNING,        // 弯道中
    CAR_CROSS,          // 十字路口
    CAR_LOST,           // 丢线
    CAR_STOPPED         // 停车
} CarState;

/* 速度档位 */
typedef struct {
    int16_t straight_speed;   // 直道速度
    int16_t turn_speed;       // 弯道速度
    int16_t cross_speed;      // 路口速度
} SpeedProfile;

/**
 * @brief 初始化循迹控制系统
 * @note  初始化方向PID和速度PID
 */
void Track_Init(void);

/**
 * @brief 循迹主控制循环
 * @note  在主循环或定时器中断中周期调用 (20ms)
 *        完成: 读传感器 -> 计算偏差 -> PID -> 输出电机
 */
void Track_Control(void);

/**
 * @brief 启动小车
 */
void Track_Start(void);

/**
 * @brief 停止小车
 */
void Track_Stop(void);

/**
 * @brief 获取当前状态
 */
CarState Track_GetState(void);

/**
 * @brief 设置速度档位
 */
void Track_SetSpeed(int16_t straight, int16_t turn);

#endif /* __TRACK_H */
