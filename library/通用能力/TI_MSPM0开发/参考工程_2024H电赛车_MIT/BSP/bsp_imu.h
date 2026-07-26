#ifndef BSP_IMU_H
#define BSP_IMU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t accelRaw[3];
    int16_t gyroRaw[3];
    int32_t accelMg[3];
    int32_t gyroMdps[3];
} BspImuSample;

bool bsp_imu_init(void);
bool bsp_imu_read(BspImuSample *sample);
bool bsp_imu_online(void);
uint8_t bsp_imu_who_am_i(void);
uint32_t bsp_imu_error_count(void);

#endif
