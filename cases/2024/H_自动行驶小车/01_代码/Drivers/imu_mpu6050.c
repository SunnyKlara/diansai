/**
 * @file    imu_mpu6050.c
 * @brief   MSPM0G3507 + I2C1 + MPU6050 驱动
 *
 * 资源映射：
 *   I2C1 SDA = PA10, SCL = PA11，速率 100 kHz
 *   INT 引脚 PA17（DMP 数据就绪触发）
 *
 * 实现策略：
 *   - 软件互补滤波代替 DMP（精度差但实现简单，3 行代码）
 *   - yaw 由 gyro_z 积分得到（短时精度高，长时间漂移）
 *   - 静态零偏标定 200 次平均
 */

#include "imu_mpu6050.h"
#include "../config.h"
#include <ti/driverlib/driverlib.h>
#include "ti_msp_dl_config.h"

#define MPU_ADDR        0x68U
#define MPU_REG_WHOAMI  0x75U
#define MPU_REG_PWR_MGMT_1   0x6BU
#define MPU_REG_SMPLRT_DIV   0x19U
#define MPU_REG_CONFIG       0x1AU
#define MPU_REG_GYRO_CONFIG  0x1BU
#define MPU_REG_ACCEL_CONFIG 0x1CU
#define MPU_REG_GYRO_ZOUT_H  0x47U
#define MPU_REG_INT_ENABLE   0x38U

/* 量程：±2000°/s → LSB = 2000/32768 = 0.0610 °/s */
#define GYRO_LSB_TO_DPS  (2000.0f / 32768.0f)

static IMU_Data s_imu = { 0 };
static float    s_yaw_offset  = 0.0f;
static float    s_gyro_z_bias = 0.0f;
static uint32_t s_last_tick_us = 0;

/* ========================================================================
 * I2C 底层
 * ===================================================================== */

static int i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    DL_I2C_fillControllerTXFIFO(I2C_INST, buf, 2);
    DL_I2C_startControllerTransfer(I2C_INST,
        MPU_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, 2);
    while (DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_BUSY) {}
    if (DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_ERROR) {
        return -1;
    }
    return 0;
}

static int i2c_read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if ((!buf) || (len == 0)) return -1;

    /* Step 1: 写寄存器地址（不发停止）*/
    DL_I2C_fillControllerTXFIFO(I2C_INST, &reg, 1);
    DL_I2C_startControllerTransfer(I2C_INST,
        MPU_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, 1);
    while (DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_BUSY) {}

    /* Step 2: 重启 + 读 */
    DL_I2C_startControllerTransfer(I2C_INST,
        MPU_ADDR, DL_I2C_CONTROLLER_DIRECTION_RX, len);
    for (uint16_t i = 0; i < len; i++) {
        while (DL_I2C_isControllerRXFIFOEmpty(I2C_INST)) {}
        buf[i] = DL_I2C_receiveControllerData(I2C_INST);
    }
    while (DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_BUSY) {}
    return 0;
}

/* ========================================================================
 * 公开 API
 * ===================================================================== */

uint8_t IMU_Init(void)
{
    /* 1. WHO_AM_I 应返回 0x68 */
    uint8_t who = 0;
    if (i2c_read_reg(MPU_REG_WHOAMI, &who, 1) != 0) return 0;
    if (who != 0x68U) return 0;

    /* 2. 复位 */
    i2c_write_reg(MPU_REG_PWR_MGMT_1, 0x80);
    delay_cycles(100000U);
    i2c_write_reg(MPU_REG_PWR_MGMT_1, 0x00);   /* 唤醒，时钟用内部 8MHz */

    /* 3. 采样率分频 = 4 → 1kHz/(1+4) = 200Hz */
    i2c_write_reg(MPU_REG_SMPLRT_DIV, 0x04);

    /* 4. DLPF 44 Hz */
    i2c_write_reg(MPU_REG_CONFIG, 0x03);

    /* 5. ±2000°/s 量程 */
    i2c_write_reg(MPU_REG_GYRO_CONFIG, 0x18);

    /* 6. ±8g 量程 */
    i2c_write_reg(MPU_REG_ACCEL_CONFIG, 0x10);

    s_imu.valid = 1;
    return 1;
}

uint8_t IMU_Calibrate(void)
{
    float sum = 0.0f;
    for (uint16_t i = 0; i < IMU_CALIB_SAMPLES; i++) {
        uint8_t buf[2] = { 0 };
        if (i2c_read_reg(MPU_REG_GYRO_ZOUT_H, buf, 2) != 0) return 0;
        int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
        sum += (float)raw * GYRO_LSB_TO_DPS;
        delay_cycles(80000U * 5U);    /* ~5 ms */
    }
    s_gyro_z_bias = sum / (float)IMU_CALIB_SAMPLES;

    /* 漂移 > 5°/s 视为没静止 */
    if ((s_gyro_z_bias > 5.0f) || (s_gyro_z_bias < -5.0f)) return 0;
    return 1;
}

void IMU_Update(void)
{
    /* 200 Hz 采样：用 SysTick 算 dt */
    uint8_t buf[2] = { 0 };
    if (i2c_read_reg(MPU_REG_GYRO_ZOUT_H, buf, 2) != 0) return;
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    float gyro_z_raw = (float)raw * GYRO_LSB_TO_DPS;
    s_imu.gyro_z = gyro_z_raw - s_gyro_z_bias;

    /* dt 固定 5 ms（与采样率匹配） */
    const float dt = 1.0f / 200.0f;
    s_imu.yaw += s_imu.gyro_z * dt;

    /* 归一化到 [-180, 180] */
    while (s_imu.yaw >  180.0f) s_imu.yaw -= 360.0f;
    while (s_imu.yaw < -180.0f) s_imu.yaw += 360.0f;
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
