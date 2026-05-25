/**
 * @file    imu_mpu6050.c
 * @brief   MPU6050 + DMP 实现
 * @note    DMP 固件加载和 I2C 读写部分依赖具体平台库，这里给出函数骨架与关键
 *          逻辑（零偏标定、yaw 归一化）。SysConfig 完成 I2C1 配置后填充
 *          read_reg/write_reg 即可。
 */

#include "imu_mpu6050.h"
#include "../config.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

static IMU_Data s_imu = { 0 };
static float    s_yaw_offset = 0.0f;
static float    s_gyro_z_bias = 0.0f;

/* ---- I2C 底层（占位）---- */
static int i2c_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg; (void)buf; (void)len;
    return -1; /* TODO_MSPM0 */
}
static int i2c_write(uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)reg; (void)buf; (void)len;
    return -1; /* TODO_MSPM0 */
}

uint8_t IMU_Init(void)
{
    /* 1. 检查 WHO_AM_I（寄存器 0x75）应返回 0x68 */
    uint8_t who = 0;
    if (i2c_read(0x75, &who, 1) != 0) return 0;
    if (who != 0x68) return 0;

    /* 2. PWR_MGMT_1 = 0x00（唤醒 + 内部 8MHz）*/
    uint8_t v;
    v = 0x00; i2c_write(0x6B, &v, 1);

    /* 3. SMPLRT_DIV = 0x04（采样 1kHz/(1+4) = 200Hz）*/
    v = 0x04; i2c_write(0x19, &v, 1);

    /* 4. CONFIG = 0x03（DLPF 44Hz，足够滤掉电机抖动）*/
    v = 0x03; i2c_write(0x1A, &v, 1);

    /* 5. GYRO_CONFIG = 0x18（±2000°/s）*/
    v = 0x18; i2c_write(0x1B, &v, 1);

    /* 6. ACCEL_CONFIG = 0x10（±8g）*/
    v = 0x10; i2c_write(0x1C, &v, 1);

    /* 7. 加载 DMP 固件 + 启动 6 轴融合
     *    这部分代码量大，常见做法是用 InvenSense 官方驱动 inv_mpu_dmp_motion_driver
     *    集成到工程。
     *    备用方案：不开 DMP，自己用互补滤波出 yaw（精度差但能用）。
     */

    s_imu.valid = 1;
    return 1;
}

uint8_t IMU_Calibrate(void)
{
    /* 静态零偏：连续读 IMU_CALIB_SAMPLES 次 gyro_z 求平均 */
    float sum = 0.0f;
    for (int i = 0; i < IMU_CALIB_SAMPLES; i++) {
        /* 读寄存器 0x47 (GYRO_ZOUT_H) 16bit */
        uint8_t buf[2] = { 0 };
        i2c_read(0x47, buf, 2);
        int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
        /* ±2000°/s 量程，LSB = 2000/32768 °/s */
        float gyro_z = (float)raw * (2000.0f / 32768.0f);
        sum += gyro_z;
        /* TODO_MSPM0: delay 5ms */
    }
    s_gyro_z_bias = sum / (float)IMU_CALIB_SAMPLES;

    /* 漂移过大表示传感器抖动或没静止 → 返回失败让用户重试 */
    if ((s_gyro_z_bias > 5.0f) || (s_gyro_z_bias < -5.0f)) return 0;
    return 1;
}

void IMU_Update(void)
{
    /* DMP 数据就绪时读取四元数，转 yaw（°）。
     * 四元数 → yaw：yaw = atan2(2(q0q3 + q1q2), 1 - 2(q2² + q3²))
     * 如果不用 DMP，则积分陀螺 z 角速度：
     *   yaw += (gyro_z - bias) * dt
     */

    /* 占位：填充随时间漂移的 yaw（仅供框架编译）*/
    /* 实际项目中由中断回调或 10ms 任务调用 */
}

float IMU_GetYaw(void)
{
    float y = s_imu.yaw - s_yaw_offset;
    while (y >  180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    return y;
}

const IMU_Data* IMU_GetData(void)
{
    return &s_imu;
}

void IMU_ZeroYaw(void)
{
    s_yaw_offset = s_imu.yaw;
}
