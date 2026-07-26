#include "APP/app_heading_control.h"

#include "APP/app_speed_control.h"

#define HEADING_ERROR_DIVISOR_MDEG       (40)
#define HEADING_RATE_DIVISOR_MDPS        (400)
#define HEADING_TURN_LIMIT_X100          (350)
#define HEADING_PIVOT_ERROR_DIVISOR_MDEG (30)
#define HEADING_PIVOT_RATE_DIVISOR_MDPS  (150)
#define HEADING_PIVOT_TURN_LIMIT_X100    (800)
#define HEADING_PIVOT_MIN_TURN_X100      (400)
#define HEADING_PIVOT_MIN_ERROR_MDEG     (3500)
#define HEADING_PIVOT_SLOW_RATE_MDPS     (8000)

static bool g_active;
static int16_t g_baseSpeed;
static int32_t g_targetYawMdeg;
static int32_t g_errorMdeg;
static int32_t g_turnX100;
static bool g_pivot;

static int32_t wrap_yaw_mdeg(int32_t value)
{
    while (value >= 180000) {
        value -= 360000;
    }
    while (value < -180000) {
        value += 360000;
    }
    return value;
}

static int32_t clamp_i32(int32_t value, int32_t limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

void app_heading_control_init(void)
{
    app_heading_control_stop();
}

void app_heading_control_start(int16_t baseSpeed, int32_t targetYawMdeg)
{
    g_baseSpeed = baseSpeed;
    g_targetYawMdeg = wrap_yaw_mdeg(targetYawMdeg);
    g_errorMdeg = 0;
    g_turnX100 = 0;
    g_pivot = false;
    g_active = true;
    app_speed_control_set_slew_step_x100(25U);
    app_speed_control_start(baseSpeed, baseSpeed);
}

void app_heading_control_start_pivot(int32_t targetYawMdeg)
{
    g_baseSpeed = 0;
    g_targetYawMdeg = wrap_yaw_mdeg(targetYawMdeg);
    g_errorMdeg = 0;
    g_turnX100 = 0;
    g_pivot = true;
    g_active = true;
    app_speed_control_set_slew_step_x100(100U);
    app_speed_control_start(0, 0);
}

void app_heading_control_stop(void)
{
    g_active = false;
    g_baseSpeed = 0;
    g_targetYawMdeg = 0;
    g_errorMdeg = 0;
    g_turnX100 = 0;
    g_pivot = false;
    app_speed_control_stop();
}

void app_heading_control_update(int32_t yawMdeg, int32_t gyroZMdps)
{
    if (!g_active) {
        return;
    }

    g_errorMdeg = wrap_yaw_mdeg(g_targetYawMdeg - yawMdeg);
    if (g_pivot) {
        g_turnX100 = clamp_i32(
            -(g_errorMdeg / HEADING_PIVOT_ERROR_DIVISOR_MDEG) +
                (gyroZMdps / HEADING_PIVOT_RATE_DIVISOR_MDPS),
            HEADING_PIVOT_TURN_LIMIT_X100);
        if ((g_errorMdeg > HEADING_PIVOT_MIN_ERROR_MDEG) &&
            (g_turnX100 > -HEADING_PIVOT_MIN_TURN_X100) &&
            (gyroZMdps < HEADING_PIVOT_SLOW_RATE_MDPS)) {
            g_turnX100 = -HEADING_PIVOT_MIN_TURN_X100;
        } else if ((g_errorMdeg < -HEADING_PIVOT_MIN_ERROR_MDEG) &&
                   (g_turnX100 < HEADING_PIVOT_MIN_TURN_X100) &&
                   (gyroZMdps > -HEADING_PIVOT_SLOW_RATE_MDPS)) {
            g_turnX100 = HEADING_PIVOT_MIN_TURN_X100;
        }
    } else {
        g_turnX100 = clamp_i32(
            -(g_errorMdeg / HEADING_ERROR_DIVISOR_MDEG) +
                (gyroZMdps / HEADING_RATE_DIVISOR_MDPS),
            HEADING_TURN_LIMIT_X100);
    }
    app_speed_control_set_targets_x100(
        ((int32_t) g_baseSpeed * 100) + g_turnX100,
        ((int32_t) g_baseSpeed * 100) - g_turnX100);
}

bool app_heading_control_active(void)
{
    return g_active;
}

int32_t app_heading_control_error_mdeg(void)
{
    return g_errorMdeg;
}

int32_t app_heading_control_turn_x100(void)
{
    return g_turnX100;
}
