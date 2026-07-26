#ifndef APP_ATTITUDE_H
#define APP_ATTITUDE_H

#include <stdbool.h>
#include <stdint.h>

#include "BSP/bsp_imu.h"

typedef enum {
    APP_ATTITUDE_OFFLINE = 0,
    APP_ATTITUDE_CALIBRATING,
    APP_ATTITUDE_READY,
} AppAttitudeState;

void app_attitude_init(void);
void app_attitude_tick_5ms(void);
bool app_attitude_start_calibration(void);
void app_attitude_zero_yaw(void);
AppAttitudeState app_attitude_state(void);
uint16_t app_attitude_calibration_samples(void);
int32_t app_attitude_yaw_mdeg(void);
int32_t app_attitude_gyro_bias_mdps(uint8_t axis);
int32_t app_attitude_gyro_rate_mdps(uint8_t axis);
const BspImuSample *app_attitude_sample(void);

#endif
