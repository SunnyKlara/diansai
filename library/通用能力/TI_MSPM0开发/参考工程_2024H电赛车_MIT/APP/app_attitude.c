#include "APP/app_attitude.h"

#define ATTITUDE_CALIBRATION_SAMPLES (400U)
#define ATTITUDE_SAMPLE_PERIOD_MS    (5)
#define ATTITUDE_GYRO_DEADBAND_MDPS  (250)

static AppAttitudeState g_state;
static BspImuSample g_sample;
static int64_t g_calibrationSum[3];
static int32_t g_gyroBiasMdps[3];
static int32_t g_gyroRateMdps[3];
static uint16_t g_calibrationSamples;
static int32_t g_yawMdeg;
static int32_t g_yawRemainder;

static int32_t apply_deadband(int32_t value)
{
    if ((value > -ATTITUDE_GYRO_DEADBAND_MDPS) &&
        (value < ATTITUDE_GYRO_DEADBAND_MDPS)) {
        return 0;
    }
    return value;
}

bool app_attitude_start_calibration(void)
{
    if (!bsp_imu_online()) {
        g_state = APP_ATTITUDE_OFFLINE;
        return false;
    }

    for (uint8_t axis = 0U; axis < 3U; axis++) {
        g_calibrationSum[axis] = 0;
        g_gyroBiasMdps[axis] = 0;
        g_gyroRateMdps[axis] = 0;
    }
    g_calibrationSamples = 0U;
    g_yawMdeg = 0;
    g_yawRemainder = 0;
    g_state = APP_ATTITUDE_CALIBRATING;
    return true;
}

void app_attitude_init(void)
{
    g_state = APP_ATTITUDE_OFFLINE;
    (void) app_attitude_start_calibration();
}

void app_attitude_tick_5ms(void)
{
    if ((g_state == APP_ATTITUDE_OFFLINE) || !bsp_imu_read(&g_sample)) {
        return;
    }

    if (g_state == APP_ATTITUDE_CALIBRATING) {
        for (uint8_t axis = 0U; axis < 3U; axis++) {
            g_calibrationSum[axis] += g_sample.gyroMdps[axis];
        }
        g_calibrationSamples++;
        if (g_calibrationSamples >= ATTITUDE_CALIBRATION_SAMPLES) {
            for (uint8_t axis = 0U; axis < 3U; axis++) {
                g_gyroBiasMdps[axis] = (int32_t)
                    (g_calibrationSum[axis] / ATTITUDE_CALIBRATION_SAMPLES);
            }
            g_yawMdeg = 0;
            g_yawRemainder = 0;
            g_state = APP_ATTITUDE_READY;
        }
        return;
    }

    int32_t yawRateMdps =
        apply_deadband(g_sample.gyroMdps[2] - g_gyroBiasMdps[2]);

    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int32_t corrected = g_sample.gyroMdps[axis] - g_gyroBiasMdps[axis];
        g_gyroRateMdps[axis] =
            ((g_gyroRateMdps[axis] * 3) + corrected) / 4;
    }
    int32_t accumulated =
        g_yawRemainder + yawRateMdps * ATTITUDE_SAMPLE_PERIOD_MS;
    g_yawMdeg += accumulated / 1000;
    g_yawRemainder = accumulated % 1000;

    while (g_yawMdeg >= 180000) {
        g_yawMdeg -= 360000;
    }
    while (g_yawMdeg < -180000) {
        g_yawMdeg += 360000;
    }
}

void app_attitude_zero_yaw(void)
{
    g_yawMdeg = 0;
    g_yawRemainder = 0;
}

AppAttitudeState app_attitude_state(void)
{
    return g_state;
}

uint16_t app_attitude_calibration_samples(void)
{
    return g_calibrationSamples;
}

int32_t app_attitude_yaw_mdeg(void)
{
    return g_yawMdeg;
}

int32_t app_attitude_gyro_bias_mdps(uint8_t axis)
{
    return (axis < 3U) ? g_gyroBiasMdps[axis] : 0;
}

int32_t app_attitude_gyro_rate_mdps(uint8_t axis)
{
    return (axis < 3U) ? g_gyroRateMdps[axis] : 0;
}

const BspImuSample *app_attitude_sample(void)
{
    return &g_sample;
}
