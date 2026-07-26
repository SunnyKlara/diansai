// -*- coding: utf-8 -*-
// QMI8658A 六轴 IMU 驱动（I2C），板载于 ESP32-P4 AIbox R5 的 I2C0（IO7/IO8）。
//
// 注意：I2C0 上还挂着 ES8389 音频 codec（地址 0x10/0x11），与本器件共用总线；
// 地址不冲突可共存，但**别在这条总线上做"全地址扫描 + 写"**。
// （另一条 I2C（IO28/IO29）上挂的是触摸 + AXP2101 PMIC 0x34，那条更危险，见 README）
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax_mg, ay_mg, az_mg;      // 加速度, 毫g
    float gx_dps, gy_dps, gz_dps;   // 角速度, 度/秒
    float temp_c;                   // 芯片温度, 摄氏度
    float a_norm_mg;                // 加速度模长, 毫g —— 静止时应 ≈1000, 是"定标是否正确"的判据
} imu_reading_t;

// 初始化：探测 I2C 地址(0x6B/0x6A) → 校验 WHO_AM_I → 配置量程与 ODR → 使能 acc+gyro
esp_err_t imu_qmi8658_init(int sda_gpio, int scl_gpio);

// 读一帧。未初始化或读失败返回非 ESP_OK
esp_err_t imu_qmi8658_read(imu_reading_t *out);

bool     imu_qmi8658_ok(void);       // 是否已成功初始化
uint8_t  imu_qmi8658_whoami(void);   // 实测到的 WHO_AM_I（期望 0x05）
uint8_t  imu_qmi8658_addr(void);     // 实测到的 I2C 地址

#ifdef __cplusplus
}
#endif
