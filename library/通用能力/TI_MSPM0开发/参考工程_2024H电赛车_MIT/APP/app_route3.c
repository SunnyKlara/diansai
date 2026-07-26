#include "APP/app_route3.h"

#include "APP/app_attitude.h"
#include "APP/app_heading_control.h"
#include "APP/app_line_control.h"

#define ROUTE3_AC_SPEED             (12)
#define ROUTE3_AC_LINE_ARM_COUNTS   (7500)
#define ROUTE3_AC_DISTANCE_GUARD    (11000)
#define ROUTE3_LINE_CONFIRM_SAMPLES (5U)
#define ROUTE3_LINE_ACQUIRE_SPEED   (8)
#define ROUTE3_C_TANGENT_YAW_MDEG   (38660)
#define ROUTE3_C_ACQUIRE_GUARD      (2500)
#define ROUTE3_B_EXIT_YAW_MDEG      (-141340)
#define ROUTE3_BD_SPEED             (12)
#define ROUTE3_BD_YAW_MDEG          (-102680)
#define ROUTE3_BD_LINE_ARM_COUNTS   (7500)
#define ROUTE3_BD_DISTANCE_GUARD    (11000)
#define ROUTE3_D_TANGENT_YAW_MDEG   (-141340)
#define ROUTE3_D_ACQUIRE_GUARD      (2500)
#define ROUTE3_TURN_TOL_MDEG        (3500)
#define ROUTE3_TURN_RATE_MAX_MDPS   (12000)
#define ROUTE3_TURN_STABLE_SAMPLES  (5U)
#define ROUTE3_TURN_GUARD_SAMPLES   (600U)
#define ROUTE3_A_EXIT_YAW_MDEG      (38660)
#define ROUTE3_END_YAW_TOL_MDEG     (35000)
#define ROUTE3_LINE_SIGNAL_STRENGTH (600U)
#define ROUTE3_ARC_RELAXED_STRENGTH (500U)
#define ROUTE3_ARC_END_MIN_COUNTS   (5000)
#define ROUTE3_END_LOST_SAMPLES     (12U)

typedef enum {
    ROUTE3_RUN_AC = 0,
    ROUTE3_RUN_ACB,
    ROUTE3_RUN_FULL,
} Route3RunMode;

static AppRoute3State g_state;
static int32_t g_distanceCounts;
static int32_t g_segmentStartCounts;
static uint8_t g_lineConfirmCount;
static uint8_t g_turnStableCount;
static uint8_t g_endpointLostCount;
static uint16_t g_stateTicks;
static Route3RunMode g_runMode;
static bool g_endpointArmed;

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

static bool yaw_near(int32_t yawMdeg, int32_t targetMdeg)
{
    int32_t error = wrap_yaw_mdeg(targetMdeg - yawMdeg);

    return (error >= -ROUTE3_END_YAW_TOL_MDEG) &&
           (error <= ROUTE3_END_YAW_TOL_MDEG);
}

static BspGrayLine protect_arc_line(BspGrayLine line)
{
    if (!line.valid &&
        (line.strength >= ROUTE3_ARC_RELAXED_STRENGTH)) {
        line.valid = true;
    }
    if (!line.valid) {
        line.valid = true;
        line.error = (int16_t) app_line_control_filtered_error();
    }
    return line;
}

static bool update_arc_endpoint(
    BspGrayLine line, int32_t exitYawMdeg)
{
    bool lineSignal = line.valid ||
                      (line.strength >= ROUTE3_ARC_RELAXED_STRENGTH);

    g_endpointArmed = g_endpointArmed ||
        ((g_distanceCounts >= ROUTE3_ARC_END_MIN_COUNTS) &&
         yaw_near(app_attitude_yaw_mdeg(), exitYawMdeg));
    if (g_endpointArmed && !lineSignal) {
        if (g_endpointLostCount < ROUTE3_END_LOST_SAMPLES) {
            g_endpointLostCount++;
        }
    } else {
        g_endpointLostCount = 0U;
    }
    return g_endpointLostCount >= ROUTE3_END_LOST_SAMPLES;
}

static int32_t encoder_average(BspEncoderSample encoder)
{
    return (encoder.leftTotal + encoder.rightTotal) / 2;
}

static int32_t absolute_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static bool update_pivot(void)
{
    int32_t gyroZ = app_attitude_gyro_rate_mdps(2U);

    app_heading_control_update(app_attitude_yaw_mdeg(), gyroZ);
    if (g_stateTicks < UINT16_MAX) {
        g_stateTicks++;
    }
    if ((absolute_i32(app_heading_control_error_mdeg()) <=
         ROUTE3_TURN_TOL_MDEG) &&
        (absolute_i32(gyroZ) <= ROUTE3_TURN_RATE_MAX_MDPS)) {
        if (g_turnStableCount < ROUTE3_TURN_STABLE_SAMPLES) {
            g_turnStableCount++;
        }
    } else {
        g_turnStableCount = 0U;
    }
    return g_turnStableCount >= ROUTE3_TURN_STABLE_SAMPLES;
}

static void start_c_turn(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    g_state = APP_ROUTE3_C_TURN_ACQUIRE;
    app_heading_control_start_pivot(ROUTE3_C_TANGENT_YAW_MDEG);
}

static void start_c_line_acquire(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_lineConfirmCount = 0U;
    g_state = APP_ROUTE3_C_LINE_ACQUIRE;
    app_heading_control_start(
        ROUTE3_LINE_ACQUIRE_SPEED, ROUTE3_C_TANGENT_YAW_MDEG);
}

static void start_b_turn(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    g_state = APP_ROUTE3_B_TURN;
    app_heading_control_start_pivot(ROUTE3_BD_YAW_MDEG);
}

static void start_bd_straight(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    g_state = APP_ROUTE3_BD_STRAIGHT;
    app_heading_control_start(ROUTE3_BD_SPEED, ROUTE3_BD_YAW_MDEG);
}

static void start_d_turn(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    g_state = APP_ROUTE3_D_TURN_ACQUIRE;
    app_heading_control_start_pivot(ROUTE3_D_TANGENT_YAW_MDEG);
}

static void start_d_line_acquire(int32_t encoderAverage)
{
    g_segmentStartCounts = encoderAverage;
    g_distanceCounts = 0;
    g_lineConfirmCount = 0U;
    g_state = APP_ROUTE3_D_LINE_ACQUIRE;
    app_heading_control_start(
        ROUTE3_LINE_ACQUIRE_SPEED, ROUTE3_D_TANGENT_YAW_MDEG);
}

void app_route3_init(void)
{
    g_state = APP_ROUTE3_IDLE;
    g_distanceCounts = 0;
    g_segmentStartCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_runMode = ROUTE3_RUN_AC;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
}

bool app_route3_start_ac(void)
{
    if (app_attitude_state() != APP_ATTITUDE_READY) {
        return false;
    }

    app_attitude_zero_yaw();
    g_distanceCounts = 0;
    g_segmentStartCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_runMode = ROUTE3_RUN_AC;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    g_state = APP_ROUTE3_AC_STRAIGHT;
    app_heading_control_start(ROUTE3_AC_SPEED, 0);
    return true;
}

bool app_route3_start_acb(void)
{
    if (!app_route3_start_ac()) {
        return false;
    }

    g_runMode = ROUTE3_RUN_ACB;
    return true;
}

bool app_route3_start_full(void)
{
    if (!app_route3_start_ac()) {
        return false;
    }

    g_runMode = ROUTE3_RUN_FULL;
    return true;
}

bool app_route3_start_cb(void)
{
    if (app_attitude_state() != APP_ATTITUDE_READY) {
        return false;
    }

    app_attitude_zero_yaw();
    g_distanceCounts = 0;
    g_segmentStartCounts = 0;
    g_lineConfirmCount = 0U;
    g_turnStableCount = 0U;
    g_stateTicks = 0U;
    g_runMode = ROUTE3_RUN_ACB;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
    start_c_turn(0);
    return true;
}

void app_route3_stop(void)
{
    app_heading_control_stop();
    app_line_control_stop();
    g_state = APP_ROUTE3_IDLE;
    g_endpointArmed = false;
    g_endpointLostCount = 0U;
}

AppRoute3Event app_route3_update_10ms(
    BspGrayLine line, BspEncoderSample encoder)
{
    int32_t encoderAverage = encoder_average(encoder);

    g_distanceCounts = encoderAverage - g_segmentStartCounts;

    if (g_state == APP_ROUTE3_AC_STRAIGHT) {
        app_heading_control_update(
            app_attitude_yaw_mdeg(), app_attitude_gyro_rate_mdps(2U));

        if (g_distanceCounts >= ROUTE3_AC_LINE_ARM_COUNTS) {
            bool lineSignal = line.valid ||
                              (line.strength >= ROUTE3_LINE_SIGNAL_STRENGTH);
            if (lineSignal) {
                if (g_lineConfirmCount < ROUTE3_LINE_CONFIRM_SAMPLES) {
                    g_lineConfirmCount++;
                }
            } else {
                g_lineConfirmCount = 0U;
            }
            if (g_lineConfirmCount >= ROUTE3_LINE_CONFIRM_SAMPLES) {
                app_heading_control_stop();
                if (g_runMode != ROUTE3_RUN_AC) {
                    start_c_turn(encoderAverage);
                    return APP_ROUTE3_EVENT_C_PASSED;
                }
                g_state = APP_ROUTE3_C_REACHED;
                return APP_ROUTE3_EVENT_C_REACHED;
            }
        }

        if (g_distanceCounts >= ROUTE3_AC_DISTANCE_GUARD) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_DISTANCE_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_C_TURN_ACQUIRE) {
        if (update_pivot()) {
            app_heading_control_stop();
            start_c_line_acquire(encoderAverage);
        } else if (g_stateTicks >= ROUTE3_TURN_GUARD_SAMPLES) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_TURN_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_C_LINE_ACQUIRE) {
        app_heading_control_update(
            app_attitude_yaw_mdeg(), app_attitude_gyro_rate_mdps(2U));
        if (line.valid ||
            (line.strength >= ROUTE3_LINE_SIGNAL_STRENGTH)) {
            if (g_lineConfirmCount < ROUTE3_LINE_CONFIRM_SAMPLES) {
                g_lineConfirmCount++;
            }
        } else {
            g_lineConfirmCount = 0U;
        }
        if (g_lineConfirmCount >= ROUTE3_LINE_CONFIRM_SAMPLES) {
            app_heading_control_stop();
            (void) app_line_control_start_fine(12, -2);
            g_state = APP_ROUTE3_CB_ARC;
            g_endpointArmed = false;
            g_endpointLostCount = 0U;
            return APP_ROUTE3_EVENT_NONE;
        }
        if (g_distanceCounts >= ROUTE3_C_ACQUIRE_GUARD) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_ACQUIRE_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_CB_ARC) {
        bool reachedEndpoint = update_arc_endpoint(
            line, ROUTE3_B_EXIT_YAW_MDEG);
        line = protect_arc_line(line);
        (void) app_line_control_update(
            line, encoder.leftDelta, encoder.rightDelta);
        if (reachedEndpoint) {
            app_line_control_stop();
            if (g_runMode == ROUTE3_RUN_FULL) {
                start_b_turn(encoderAverage);
                return APP_ROUTE3_EVENT_B_PASSED;
            }
            g_state = APP_ROUTE3_B_REACHED;
            return APP_ROUTE3_EVENT_B_REACHED;
        }
    }

    if (g_state == APP_ROUTE3_B_TURN) {
        if (update_pivot()) {
            app_heading_control_stop();
            start_bd_straight(encoderAverage);
        } else if (g_stateTicks >= ROUTE3_TURN_GUARD_SAMPLES) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_TURN_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_BD_STRAIGHT) {
        app_heading_control_update(
            app_attitude_yaw_mdeg(), app_attitude_gyro_rate_mdps(2U));

        if (g_distanceCounts >= ROUTE3_BD_LINE_ARM_COUNTS) {
            bool lineSignal = line.valid ||
                              (line.strength >= ROUTE3_LINE_SIGNAL_STRENGTH);
            if (lineSignal) {
                if (g_lineConfirmCount < ROUTE3_LINE_CONFIRM_SAMPLES) {
                    g_lineConfirmCount++;
                }
            } else {
                g_lineConfirmCount = 0U;
            }
            if (g_lineConfirmCount >= ROUTE3_LINE_CONFIRM_SAMPLES) {
                app_heading_control_stop();
                start_d_turn(encoderAverage);
                return APP_ROUTE3_EVENT_D_PASSED;
            }
        }

        if (g_distanceCounts >= ROUTE3_BD_DISTANCE_GUARD) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_BD_DISTANCE_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_D_TURN_ACQUIRE) {
        if (update_pivot()) {
            app_heading_control_stop();
            start_d_line_acquire(encoderAverage);
        } else if (g_stateTicks >= ROUTE3_TURN_GUARD_SAMPLES) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_TURN_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_D_LINE_ACQUIRE) {
        app_heading_control_update(
            app_attitude_yaw_mdeg(), app_attitude_gyro_rate_mdps(2U));
        if (line.valid ||
            (line.strength >= ROUTE3_LINE_SIGNAL_STRENGTH)) {
            if (g_lineConfirmCount < ROUTE3_LINE_CONFIRM_SAMPLES) {
                g_lineConfirmCount++;
            }
        } else {
            g_lineConfirmCount = 0U;
        }
        if (g_lineConfirmCount >= ROUTE3_LINE_CONFIRM_SAMPLES) {
            app_heading_control_stop();
            (void) app_line_control_start_fine(12, 2);
            g_state = APP_ROUTE3_DA_ARC;
            g_endpointArmed = false;
            g_endpointLostCount = 0U;
            return APP_ROUTE3_EVENT_NONE;
        }
        if (g_distanceCounts >= ROUTE3_D_ACQUIRE_GUARD) {
            app_heading_control_stop();
            g_state = APP_ROUTE3_FAILED;
            return APP_ROUTE3_EVENT_D_ACQUIRE_GUARD;
        }
        return APP_ROUTE3_EVENT_NONE;
    }

    if (g_state == APP_ROUTE3_DA_ARC) {
        bool reachedEndpoint = update_arc_endpoint(
            line, ROUTE3_A_EXIT_YAW_MDEG);
        line = protect_arc_line(line);
        (void) app_line_control_update(
            line, encoder.leftDelta, encoder.rightDelta);
        if (reachedEndpoint) {
            app_line_control_stop();
            g_state = APP_ROUTE3_A_REACHED;
            return APP_ROUTE3_EVENT_A_REACHED;
        }
    }
    return APP_ROUTE3_EVENT_NONE;
}

AppRoute3State app_route3_state(void)
{
    return g_state;
}

int32_t app_route3_distance_counts(void)
{
    return g_distanceCounts;
}

bool app_route3_endpoint_armed(void)
{
    return g_endpointArmed;
}

uint8_t app_route3_endpoint_lost_count(void)
{
    return g_endpointLostCount;
}
