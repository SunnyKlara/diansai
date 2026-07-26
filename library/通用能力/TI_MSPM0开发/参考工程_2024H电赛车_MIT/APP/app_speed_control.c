#include "APP/app_speed_control.h"

#include "BSP/bsp_motor.h"

#define SPEED_TARGET_LIMIT (30)
#define SPEED_OUTPUT_LIMIT (400)
#define SPEED_INTEGRAL_LIMIT_X100 (15000)

static bool g_active;
static int32_t g_leftRequestX100;
static int32_t g_rightRequestX100;
static int32_t g_leftTargetX100;
static int32_t g_rightTargetX100;
static int32_t g_leftIntegralX100;
static int32_t g_rightIntegralX100;
static uint16_t g_kpX100;
static uint16_t g_kiX100;
static uint16_t g_feedforwardX100;
static uint16_t g_slewStepX100;

static int16_t clamp_i16(int32_t value, int16_t limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return (int16_t) value;
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

static int32_t slew_target_x100(int32_t current, int32_t requested)
{
    if (current < requested) {
        current += g_slewStepX100;
        return (current > requested) ? requested : current;
    }
    if (current > requested) {
        current -= g_slewStepX100;
        return (current < requested) ? requested : current;
    }
    return current;
}

static int16_t update_wheel(
    int32_t targetX100, int32_t measured, int32_t *integralX100)
{
    int32_t errorX100 = targetX100 - (measured * 100);
    int32_t feedforward = (targetX100 * g_feedforwardX100) / 10000;
    int32_t proportional = (errorX100 * g_kpX100) / 10000;

    if (targetX100 == 0) {
        *integralX100 = 0;
        return 0;
    }

    *integralX100 += (errorX100 * g_kiX100) / 100;
    *integralX100 = clamp_i32(
        *integralX100, SPEED_INTEGRAL_LIMIT_X100);

    return clamp_i16(feedforward + proportional + (*integralX100 / 100),
                     SPEED_OUTPUT_LIMIT);
}

void app_speed_control_init(void)
{
    g_kpX100 = 300U;
    g_kiX100 = 10U;
    g_feedforwardX100 = 850U;
    g_slewStepX100 = 100U;
    app_speed_control_stop();
}

void app_speed_control_start(int16_t leftTarget, int16_t rightTarget)
{
    app_speed_control_set_targets(leftTarget, rightTarget);
    g_leftTargetX100 = 0;
    g_rightTargetX100 = 0;
    g_leftIntegralX100 = 0;
    g_rightIntegralX100 = 0;
    g_active = true;
}

void app_speed_control_set_targets(int16_t leftTarget, int16_t rightTarget)
{
    app_speed_control_set_targets_x100(
        (int32_t) leftTarget * 100, (int32_t) rightTarget * 100);
}

void app_speed_control_set_targets_x100(
    int32_t leftTargetX100, int32_t rightTargetX100)
{
    g_leftRequestX100 = clamp_i32(
        leftTargetX100, SPEED_TARGET_LIMIT * 100);
    g_rightRequestX100 = clamp_i32(
        rightTargetX100, SPEED_TARGET_LIMIT * 100);
}

void app_speed_control_set_slew_step_x100(uint16_t stepX100)
{
    if (stepX100 < 10U) {
        stepX100 = 10U;
    } else if (stepX100 > 100U) {
        stepX100 = 100U;
    }
    g_slewStepX100 = stepX100;
}

void app_speed_control_stop(void)
{
    g_active = false;
    g_leftRequestX100 = 0;
    g_rightRequestX100 = 0;
    g_leftTargetX100 = 0;
    g_rightTargetX100 = 0;
    g_leftIntegralX100 = 0;
    g_rightIntegralX100 = 0;
    bsp_motor_stop();
}

void app_speed_control_update(int32_t leftDelta, int32_t rightDelta)
{
    if (!g_active) {
        return;
    }

    g_leftTargetX100 =
        slew_target_x100(g_leftTargetX100, g_leftRequestX100);
    g_rightTargetX100 =
        slew_target_x100(g_rightTargetX100, g_rightRequestX100);
    bsp_motor_set(
        update_wheel(g_leftTargetX100, leftDelta, &g_leftIntegralX100),
        update_wheel(g_rightTargetX100, rightDelta, &g_rightIntegralX100));
}

bool app_speed_control_active(void)
{
    return g_active;
}

void app_speed_control_set_gains(
    uint16_t kpX100, uint16_t kiX100, uint16_t feedforwardX100)
{
    g_kpX100 = (kpX100 > 1000U) ? 1000U : kpX100;
    g_kiX100 = (kiX100 > 100U) ? 100U : kiX100;
    g_feedforwardX100 =
        (feedforwardX100 > 1500U) ? 1500U : feedforwardX100;
}

uint16_t app_speed_control_kp_x100(void)
{
    return g_kpX100;
}

uint16_t app_speed_control_ki_x100(void)
{
    return g_kiX100;
}

uint16_t app_speed_control_feedforward_x100(void)
{
    return g_feedforwardX100;
}

int16_t app_speed_control_left_request(void)
{
    return (int16_t) (g_leftRequestX100 / 100);
}

int16_t app_speed_control_right_request(void)
{
    return (int16_t) (g_rightRequestX100 / 100);
}

int16_t app_speed_control_left_target(void)
{
    return (int16_t) (g_leftTargetX100 / 100);
}

int16_t app_speed_control_right_target(void)
{
    return (int16_t) (g_rightTargetX100 / 100);
}
