#include "APP/app_line_control.h"

#include "APP/app_speed_control.h"

#define LINE_BASE_SPEED_MIN (8)
#define LINE_BASE_SPEED_MAX (20)
#define LINE_TURN_LIMIT (8)
#define LINE_CURVE_BIAS_LIMIT (6)
#define LINE_LOST_STOP_SAMPLES (5U)
#define LINE_YAW_DAMPING_X100 (30)

static bool g_active;
static int16_t g_baseSpeed;
static int16_t g_turn;
static int32_t g_turnX100;
static int32_t g_filteredError;
static int32_t g_previousError;
static int32_t g_filteredDerivative;
static uint8_t g_lostCount;
static bool g_errorReady;
static int16_t g_curveBias;
static int16_t g_yawError;
static bool g_fineControl;
static uint16_t g_kpX10000;
static uint16_t g_kdX10000;

static int16_t clamp_i16(int32_t value, int16_t minimum, int16_t maximum)
{
    if (value > maximum) {
        return maximum;
    }
    if (value < minimum) {
        return minimum;
    }
    return (int16_t) value;
}

void app_line_control_init(void)
{
    g_kpX10000 = 40U;
    g_kdX10000 = 220U;
    app_line_control_stop();
}

bool app_line_control_start(int16_t baseSpeed)
{
    return app_line_control_start_with_bias(baseSpeed, 0);
}

bool app_line_control_start_with_bias(int16_t baseSpeed, int16_t curveBias)
{
    if ((baseSpeed < LINE_BASE_SPEED_MIN) ||
        (baseSpeed > LINE_BASE_SPEED_MAX) ||
        (curveBias < -LINE_CURVE_BIAS_LIMIT) ||
        (curveBias > LINE_CURVE_BIAS_LIMIT)) {
        return false;
    }

    g_baseSpeed = baseSpeed;
    g_curveBias = curveBias;
    g_yawError = 0;
    g_fineControl = false;
    g_turn = 0;
    g_turnX100 = 0;
    g_filteredError = 0;
    g_previousError = 0;
    g_filteredDerivative = 0;
    g_lostCount = 0;
    g_errorReady = false;
    g_active = true;
    app_speed_control_set_slew_step_x100(100U);
    app_speed_control_start(
        (int16_t) (baseSpeed + curveBias),
        (int16_t) (baseSpeed - curveBias));
    return true;
}

bool app_line_control_start_fine(int16_t baseSpeed, int16_t curveBias)
{
    if (!app_line_control_start_with_bias(baseSpeed, curveBias)) {
        return false;
    }
    g_fineControl = true;
    return true;
}

void app_line_control_stop(void)
{
    g_active = false;
    g_baseSpeed = 0;
    g_turn = 0;
    g_turnX100 = 0;
    g_filteredError = 0;
    g_previousError = 0;
    g_filteredDerivative = 0;
    g_lostCount = 0;
    g_errorReady = false;
    g_curveBias = 0;
    g_yawError = 0;
    g_fineControl = false;
    app_speed_control_stop();
}

bool app_line_control_update(
    BspGrayLine line, int32_t leftDelta, int32_t rightDelta)
{
    int32_t derivative;
    int32_t desiredTurn;
    int32_t desiredTurnX100;
    int32_t turn;

    if (!g_active) {
        return false;
    }

    if (!line.valid) {
        if (g_lostCount < UINT8_MAX) {
            g_lostCount++;
        }
        if (g_lostCount >= LINE_LOST_STOP_SAMPLES) {
            app_line_control_stop();
            return false;
        }
        return true;
    }

    g_lostCount = 0;
    if (!g_errorReady) {
        g_filteredError = line.error;
        g_errorReady = true;
    } else {
        g_filteredError = (g_filteredError + line.error) / 2;
    }
    derivative = g_filteredError - g_previousError;
    g_filteredDerivative = ((g_filteredDerivative * 3) + derivative) / 4;
    g_previousError = g_filteredError;

    desiredTurnX100 = ((int32_t) g_curveBias * 100) +
        (((int32_t) g_kpX10000 * g_filteredError +
          (int32_t) g_kdX10000 * g_filteredDerivative) / 100);
    desiredTurnX100 = clamp_i16(
        desiredTurnX100, -LINE_TURN_LIMIT * 100, LINE_TURN_LIMIT * 100);
    desiredTurn = desiredTurnX100 / 100;
    desiredTurn = clamp_i16(
        desiredTurn, -LINE_TURN_LIMIT, LINE_TURN_LIMIT);
    if (g_fineControl) {
        int32_t yawErrorX100 = ((leftDelta - rightDelta) * 100) -
                               (2 * desiredTurnX100);

        g_yawError = (int16_t) (yawErrorX100 / 100);
        g_turnX100 = clamp_i16(desiredTurnX100 -
            ((yawErrorX100 * LINE_YAW_DAMPING_X100) / 100),
            -LINE_TURN_LIMIT * 100, LINE_TURN_LIMIT * 100);
        turn = g_turnX100 / 100;
    } else {
        g_yawError = (int16_t) ((leftDelta - rightDelta) -
                                (2 * desiredTurn));
        turn = desiredTurn -
            (((int32_t) g_yawError * LINE_YAW_DAMPING_X100) / 100);
        g_turnX100 = clamp_i16(
            turn, -LINE_TURN_LIMIT, LINE_TURN_LIMIT) * 100;
    }
    g_turn = clamp_i16(turn, -LINE_TURN_LIMIT, LINE_TURN_LIMIT);
    if (g_fineControl) {
        app_speed_control_set_targets_x100(
            ((int32_t) g_baseSpeed * 100) + g_turnX100,
            ((int32_t) g_baseSpeed * 100) - g_turnX100);
    } else {
        app_speed_control_set_targets(
            (int16_t) (g_baseSpeed + g_turn),
            (int16_t) (g_baseSpeed - g_turn));
    }
    return true;
}

bool app_line_control_active(void)
{
    return g_active;
}

void app_line_control_set_gains(uint16_t kpX10000, uint16_t kdX10000)
{
    g_kpX10000 = (kpX10000 > 200U) ? 200U : kpX10000;
    g_kdX10000 = (kdX10000 > 500U) ? 500U : kdX10000;
}

uint16_t app_line_control_kp_x10000(void)
{
    return g_kpX10000;
}

uint16_t app_line_control_kd_x10000(void)
{
    return g_kdX10000;
}

int16_t app_line_control_base_speed(void)
{
    return g_baseSpeed;
}

int16_t app_line_control_turn(void)
{
    return g_turn;
}

int32_t app_line_control_turn_x100(void)
{
    return g_turnX100;
}

int16_t app_line_control_filtered_error(void)
{
    return (int16_t) g_filteredError;
}

int16_t app_line_control_curve_bias(void)
{
    return g_curveBias;
}

int16_t app_line_control_yaw_error(void)
{
    return g_yawError;
}

uint8_t app_line_control_lost_count(void)
{
    return g_lostCount;
}
