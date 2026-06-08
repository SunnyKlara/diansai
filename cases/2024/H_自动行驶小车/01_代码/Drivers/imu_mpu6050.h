/**
 * @file    imu_mpu6050.h
 * @brief   MPU6050 + DMP 姿态读取
 *
 *  接口策略：暴露 yaw/pitch/roll 浮点角度，单位 °，范围 [-180, 180]。
 *  调用流程：
 *      IMU_Init()       → 上电初始化 + DMP 加载
 *      IMU_Calibrate()  → 静态零偏标定（车保持不动 ~2 秒）
 *      IMU_Update()     → 中断回调中读取最新数据（200Hz）
 *      IMU_GetYaw()     → 主循环获取最新 yaw
 */

#ifndef __IMU_MPU6050_H
#define __IMU_MPU6050_H

#include <stdint.h>

typedef struct {
    float yaw;           /* °，初始航向 0 */
    float pitch;
    float roll;
    float gyro_z;        /* 当前 z 轴角速度 °/s */
    uint8_t valid;       /* 0=尚未就绪 */
} IMU_Data;

uint8_t IMU_Init(void);                /* 返回 0=失败（找不到芯片）*/
uint8_t IMU_Calibrate(void);           /* 返回 0=失败（漂移过大）*/
void    IMU_Update(void);              /* DMP 数据就绪中断中调用 */

float   IMU_GetYaw(void);
const IMU_Data* IMU_GetData(void);

/** @brief 把当前 yaw 设为 0（段开始时使用）*/
void    IMU_ZeroYaw(void);

#endif /* __IMU_MPU6050_H */
