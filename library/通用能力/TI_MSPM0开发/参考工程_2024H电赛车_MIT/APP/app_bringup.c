#include "APP/app_bringup.h"
#include "APP/app_attitude.h"
#include "APP/app_heading_control.h"
#include "APP/app_line_control.h"
#include "APP/app_route3.h"
#include "APP/app_speed_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "BSP/bsp_encoder.h"
#include "BSP/bsp_gray.h"
#include "BSP/bsp_motor.h"
#include "BSP/bsp_timebase.h"
#include "BSP/bsp_uart.h"
#include "BSP/bsp_user.h"

typedef enum {
    TEST_IDLE = 0,
    TEST_LEFT,
    TEST_GAP,
    TEST_RIGHT,
    TEST_MANUAL,
    TEST_SPEED,
    TEST_FOLLOW,
    TEST_ROUTE3_AC,
    TEST_ROUTE3_ACB,
    TEST_ROUTE3_FULL,
    TEST_ROUTE3_CB,
} TestPhase;

static TestPhase g_phase;
static uint32_t g_phaseDeadline;
static uint16_t g_div10ms;
static uint16_t g_div5ms;
static uint16_t g_div500ms;
static uint16_t g_div200ms;
static BspEncoderSample g_encoder;
static BspGrayLine g_line;
static uint32_t g_calibrationDeadline;
static bool g_buttonCandidate;
static bool g_buttonStable;
static uint8_t g_buttonCount;

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t) (now - deadline) >= 0);
}

static const char *phase_name(void)
{
    switch (g_phase) {
    case TEST_IDLE:
        return "idle";
    case TEST_LEFT:
        return "left";
    case TEST_GAP:
        return "gap";
    case TEST_RIGHT:
        return "right";
    case TEST_MANUAL:
        return "manual";
    case TEST_SPEED:
        return "speed";
    case TEST_FOLLOW:
        return "follow";
    case TEST_ROUTE3_AC:
        return "route3_ac";
    case TEST_ROUTE3_ACB:
        return "route3_acb";
    case TEST_ROUTE3_FULL:
        return "route3_full";
    case TEST_ROUTE3_CB:
        return "route3_cb";
    default:
        return "unknown";
    }
}

static void write_gray_values(void)
{
    const uint16_t *gray = bsp_gray_values();

    bsp_uart_write("gray=");
    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        if (i != 0U) {
            bsp_uart_write_char(',');
        }
        bsp_uart_write_u32(gray[i]);
    }
}

static void write_u16_list(const uint16_t *values)
{
    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        if (i != 0U) {
            bsp_uart_write_char(',');
        }
        bsp_uart_write_u32(values[i]);
    }
}

static void write_line(void)
{
    bsp_uart_write("line=");
    bsp_uart_write_i32(g_line.error);
    bsp_uart_write(" str=");
    bsp_uart_write_u32(g_line.strength);
    bsp_uart_write(" lost=");
    bsp_uart_write_u32(g_line.valid ? 0U : 1U);
    bsp_uart_write(" norm=");
    write_u16_list(g_line.normalized);
}

static void write_calibration(void)
{
    bsp_uart_write("cal=");
    if (bsp_gray_calibration_active()) {
        bsp_uart_write("active");
    } else if (bsp_gray_is_calibrated()) {
        bsp_uart_write("ready");
    } else {
        bsp_uart_write("invalid");
    }
    bsp_uart_write(" valid=");
    bsp_uart_write_u32(bsp_gray_calibration_valid_channels());
    bsp_uart_write(" min=");
    write_u16_list(bsp_gray_calibration_min());
    bsp_uart_write(" max=");
    write_u16_list(bsp_gray_calibration_max());
    bsp_uart_write("\r\n");
}

static void finish_calibration(void)
{
    if (bsp_gray_calibration_stop()) {
        bsp_uart_write("CAL ready\r\n");
    } else {
        bsp_uart_write("CAL failed: sweep black line across all sensors\r\n");
    }
    write_calibration();
}

static void write_status(void)
{
    bsp_uart_write("t=");
    bsp_uart_write_u32(bsp_timebase_now_ms());
    bsp_uart_write(" phase=");
    bsp_uart_write(phase_name());
    bsp_uart_write(" pwm=");
    bsp_uart_write_i32(bsp_motor_left_command());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(bsp_motor_right_command());
    bsp_uart_write(" enc_d=");
    bsp_uart_write_i32(g_encoder.leftDelta);
    bsp_uart_write_char(',');
    bsp_uart_write_i32(g_encoder.rightDelta);
    bsp_uart_write(" enc_t=");
    bsp_uart_write_i32(g_encoder.leftTotal);
    bsp_uart_write_char(',');
    bsp_uart_write_i32(g_encoder.rightTotal);
    bsp_uart_write(" inv=");
    bsp_uart_write_u32(g_encoder.leftInvalid);
    bsp_uart_write_char(',');
    bsp_uart_write_u32(g_encoder.rightInvalid);
    bsp_uart_write(" line=");
    bsp_uart_write_i32(g_line.error);
    bsp_uart_write(" filt=");
    bsp_uart_write_i32(app_line_control_filtered_error());
    bsp_uart_write(" str=");
    bsp_uart_write_u32(g_line.strength);
    bsp_uart_write(" lost=");
    bsp_uart_write_u32(g_line.valid ? 0U : 1U);
    bsp_uart_write_char(' ');
    write_gray_values();
    bsp_uart_write(" btn=");
    bsp_uart_write_u32(bsp_user_button_pressed() ? 1U : 0U);
    bsp_uart_write(" rx=");
    bsp_uart_write_u32(bsp_uart_rx_count());
    bsp_uart_write("\r\n");
}

static void write_speed_status(void)
{
    bsp_uart_write("t=");
    bsp_uart_write_u32(bsp_timebase_now_ms());
    bsp_uart_write(" sp=");
    bsp_uart_write_i32(app_speed_control_left_request());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(app_speed_control_right_request());
    bsp_uart_write(" ramp=");
    bsp_uart_write_i32(app_speed_control_left_target());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(app_speed_control_right_target());
    bsp_uart_write(" vel=");
    bsp_uart_write_i32(g_encoder.leftDelta);
    bsp_uart_write_char(',');
    bsp_uart_write_i32(g_encoder.rightDelta);
    bsp_uart_write(" pwm=");
    bsp_uart_write_i32(bsp_motor_left_command());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(bsp_motor_right_command());
    bsp_uart_write(" inv=");
    bsp_uart_write_u32(g_encoder.leftInvalid);
    bsp_uart_write_char(',');
    bsp_uart_write_u32(g_encoder.rightInvalid);
    bsp_uart_write("\r\n");
}

static void write_pi_gains(void)
{
    bsp_uart_write("pi kp100=");
    bsp_uart_write_u32(app_speed_control_kp_x100());
    bsp_uart_write(" ki100=");
    bsp_uart_write_u32(app_speed_control_ki_x100());
    bsp_uart_write(" ff100=");
    bsp_uart_write_u32(app_speed_control_feedforward_x100());
    bsp_uart_write("\r\n");
}

static void write_line_gains(void)
{
    bsp_uart_write("lpd kp10000=");
    bsp_uart_write_u32(app_line_control_kp_x10000());
    bsp_uart_write(" kd10000=");
    bsp_uart_write_u32(app_line_control_kd_x10000());
    bsp_uart_write("\r\n");
}

static const char *attitude_state_name(void)
{
    switch (app_attitude_state()) {
    case APP_ATTITUDE_CALIBRATING:
        return "calibrating";
    case APP_ATTITUDE_READY:
        return "ready";
    case APP_ATTITUDE_OFFLINE:
    default:
        return "offline";
    }
}

static void write_imu_status(void)
{
    const BspImuSample *sample = app_attitude_sample();

    bsp_uart_write("imu=");
    bsp_uart_write(attitude_state_name());
    bsp_uart_write(" who=");
    bsp_uart_write_u32(bsp_imu_who_am_i());
    bsp_uart_write(" cal=");
    bsp_uart_write_u32(app_attitude_calibration_samples());
    bsp_uart_write(" acc_mg=");
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        if (axis != 0U) {
            bsp_uart_write_char(',');
        }
        bsp_uart_write_i32(sample->accelMg[axis]);
    }
    bsp_uart_write(" gyro_mdps=");
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        if (axis != 0U) {
            bsp_uart_write_char(',');
        }
        bsp_uart_write_i32(sample->gyroMdps[axis]);
    }
    bsp_uart_write(" bias=");
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        if (axis != 0U) {
            bsp_uart_write_char(',');
        }
        bsp_uart_write_i32(app_attitude_gyro_bias_mdps(axis));
    }
    bsp_uart_write(" yaw_mdeg=");
    bsp_uart_write_i32(app_attitude_yaw_mdeg());
    bsp_uart_write(" err=");
    bsp_uart_write_u32(bsp_imu_error_count());
    bsp_uart_write("\r\n");
}

static void write_follow_status(void)
{
    bsp_uart_write("t=");
    bsp_uart_write_u32(bsp_timebase_now_ms());
    bsp_uart_write(" line=");
    bsp_uart_write_i32(g_line.error);
    bsp_uart_write(" str=");
    bsp_uart_write_u32(g_line.strength);
    bsp_uart_write(" lost=");
    bsp_uart_write_u32(app_line_control_lost_count());
    bsp_uart_write(" turn=");
    bsp_uart_write_i32(app_line_control_turn());
    bsp_uart_write(" turn100=");
    bsp_uart_write_i32(app_line_control_turn_x100());
    bsp_uart_write(" bias=");
    bsp_uart_write_i32(app_line_control_curve_bias());
    bsp_uart_write(" yawerr=");
    bsp_uart_write_i32(app_line_control_yaw_error());
    bsp_uart_write(" gz=");
    bsp_uart_write_i32(app_attitude_gyro_rate_mdps(2U));
    bsp_uart_write(" yaw=");
    bsp_uart_write_i32(app_attitude_yaw_mdeg());
    bsp_uart_write(" sp=");
    bsp_uart_write_i32(app_speed_control_left_request());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(app_speed_control_right_request());
    bsp_uart_write(" vel=");
    bsp_uart_write_i32(g_encoder.leftDelta);
    bsp_uart_write_char(',');
    bsp_uart_write_i32(g_encoder.rightDelta);
    bsp_uart_write(" pwm=");
    bsp_uart_write_i32(bsp_motor_left_command());
    bsp_uart_write_char(',');
    bsp_uart_write_i32(bsp_motor_right_command());
    bsp_uart_write("\r\n");
}

static void write_route3_status(void)
{
    bsp_uart_write("t=");
    bsp_uart_write_u32(bsp_timebase_now_ms());
    bsp_uart_write(" route_state=");
    bsp_uart_write_u32((uint32_t) app_route3_state());
    bsp_uart_write(" dist=");
    bsp_uart_write_i32(app_route3_distance_counts());
    bsp_uart_write(" yaw=");
    bsp_uart_write_i32(app_attitude_yaw_mdeg());
    bsp_uart_write(" herr=");
    bsp_uart_write_i32(app_heading_control_error_mdeg());
    bsp_uart_write(" turn100=");
    bsp_uart_write_i32(app_heading_control_turn_x100());
    bsp_uart_write(" line=");
    bsp_uart_write_i32(g_line.error);
    bsp_uart_write(" str=");
    bsp_uart_write_u32(g_line.strength);
    bsp_uart_write(" seen=");
    bsp_uart_write_u32(g_line.valid ? 1U : 0U);
    bsp_uart_write(" endarm=");
    bsp_uart_write_u32(app_route3_endpoint_armed() ? 1U : 0U);
    bsp_uart_write(" endlost=");
    bsp_uart_write_u32(app_route3_endpoint_lost_count());
    bsp_uart_write(" vel=");
    bsp_uart_write_i32(g_encoder.leftDelta);
    bsp_uart_write_char(',');
    bsp_uart_write_i32(g_encoder.rightDelta);
    bsp_uart_write("\r\n");
}

static void stop_test(void)
{
    app_route3_stop();
    app_heading_control_stop();
    app_line_control_stop();
    app_speed_control_stop();
    bsp_motor_stop();
    g_phase = TEST_IDLE;
    bsp_uart_write("OK stopped\r\n");
}

static void start_route3_ac_test(void)
{
    app_line_control_stop();
    app_route3_stop();
    bsp_encoder_reset();
    g_encoder = bsp_encoder_sample_window();
    if (!app_route3_start_ac()) {
        g_phase = TEST_IDLE;
        bsp_uart_write("ERR IMU not ready; keep still for calibration\r\n");
        return;
    }

    g_phase = TEST_ROUTE3_AC;
    g_phaseDeadline = bsp_timebase_now_ms() + 12000U;
    g_div10ms = 0;
    g_div200ms = 0;
    bsp_uart_write("ROUTE3 AC started: heading zero, seek C arc\r\n");
}

static void start_route3_cb_test(void)
{
    app_line_control_stop();
    app_route3_stop();
    bsp_encoder_reset();
    g_encoder = bsp_encoder_sample_window();
    if (!app_route3_start_cb()) {
        g_phase = TEST_IDLE;
        bsp_uart_write("ERR IMU not ready; keep still for calibration\r\n");
        return;
    }

    g_phase = TEST_ROUTE3_CB;
    g_phaseDeadline = bsp_timebase_now_ms() + 15000U;
    g_div10ms = 0;
    g_div200ms = 0;
    bsp_uart_write("ROUTE3 CB started: forward turn, acquire arc\r\n");
}

static void start_route3_acb_test(void)
{
    app_line_control_stop();
    app_route3_stop();
    bsp_encoder_reset();
    g_encoder = bsp_encoder_sample_window();
    if (!app_route3_start_acb()) {
        g_phase = TEST_IDLE;
        bsp_uart_write("ERR IMU not ready; keep still for calibration\r\n");
        return;
    }

    g_phase = TEST_ROUTE3_ACB;
    g_phaseDeadline = bsp_timebase_now_ms() + 25000U;
    g_div10ms = 0;
    g_div200ms = 0;
    bsp_uart_write("ROUTE3 ACB started: straight to C, then arc to B\r\n");
}

static void start_route3_full_test(void)
{
    app_line_control_stop();
    app_route3_stop();
    bsp_encoder_reset();
    g_encoder = bsp_encoder_sample_window();
    if (!app_route3_start_full()) {
        g_phase = TEST_IDLE;
        bsp_uart_write("ERR IMU not ready; keep still for calibration\r\n");
        return;
    }

    g_phase = TEST_ROUTE3_FULL;
    g_phaseDeadline = bsp_timebase_now_ms() + 45000U;
    g_div10ms = 0;
    g_div200ms = 0;
    bsp_uart_write("ROUTE3 full started: A-C-B-D-A\r\n");
}

static void start_test(void)
{
    app_line_control_stop();
    app_speed_control_stop();
    bsp_encoder_reset();
    bsp_motor_set(220, 0);
    g_phase = TEST_LEFT;
    g_phaseDeadline = bsp_timebase_now_ms() + 1000U;
    bsp_uart_write("TEST left wheel 1s\r\n");
}

static void start_button_route3_full(void)
{
    start_route3_full_test();
}

static bool parse_i32(const char **cursor, int32_t *value)
{
    bool negative = false;
    int32_t result = 0;
    bool found = false;

    while (**cursor == ' ') {
        (*cursor)++;
    }
    if (**cursor == '-') {
        negative = true;
        (*cursor)++;
    } else if (**cursor == '+') {
        (*cursor)++;
    }
    while ((**cursor >= '0') && (**cursor <= '9')) {
        result = (result * 10) + (**cursor - '0');
        (*cursor)++;
        found = true;
    }
    if (!found) {
        return false;
    }
    *value = negative ? -result : result;
    return true;
}

static void process_command(const char *command)
{
    if (strcmp(command, "help") == 0) {
        bsp_uart_write("help status gray line cal [start|stop] enc imu [cal] yaw zero route3 [ac|acb|cb|full] test stop motor <L> <R> speed <L> <R> pi [Kp100 Ki100 FF100] follow <8..20> followrun <8..20> followarc <8..20> <-6..6> followfine <8..20> <-6..6> lpd [Kp10000 Kd10000]\r\n");
    } else if (strcmp(command, "status") == 0) {
        write_status();
    } else if (strcmp(command, "gray") == 0) {
        write_gray_values();
        bsp_uart_write("\r\n");
    } else if (strcmp(command, "line") == 0) {
        write_line();
        bsp_uart_write("\r\n");
    } else if (strcmp(command, "cal") == 0) {
        write_calibration();
    } else if (strcmp(command, "cal start") == 0) {
        app_line_control_stop();
        app_speed_control_stop();
        g_phase = TEST_IDLE;
        bsp_gray_calibration_start();
        g_calibrationDeadline = bsp_timebase_now_ms() + 8000U;
        bsp_uart_write("CAL started: sweep line across all sensors for 8s\r\n");
    } else if (strcmp(command, "cal stop") == 0) {
        finish_calibration();
    } else if (strcmp(command, "enc") == 0) {
        bsp_uart_write("enc_d=");
        bsp_uart_write_i32(g_encoder.leftDelta);
        bsp_uart_write_char(',');
        bsp_uart_write_i32(g_encoder.rightDelta);
        bsp_uart_write(" enc_t=");
        bsp_uart_write_i32(g_encoder.leftTotal);
        bsp_uart_write_char(',');
        bsp_uart_write_i32(g_encoder.rightTotal);
        bsp_uart_write("\r\n");
    } else if (strcmp(command, "imu") == 0) {
        write_imu_status();
    } else if (strcmp(command, "imu cal") == 0) {
        if (app_attitude_start_calibration()) {
            bsp_uart_write("IMU calibration started: keep car still for 2s\r\n");
        } else {
            bsp_uart_write("ERR IMU offline\r\n");
        }
    } else if (strcmp(command, "yaw zero") == 0) {
        app_attitude_zero_yaw();
        bsp_uart_write("OK yaw zeroed\r\n");
    } else if (strcmp(command, "route3 ac") == 0) {
        start_route3_ac_test();
    } else if (strcmp(command, "route3 acb") == 0) {
        start_route3_acb_test();
    } else if (strcmp(command, "route3 full") == 0) {
        start_route3_full_test();
    } else if (strcmp(command, "route3 cb") == 0) {
        start_route3_cb_test();
    } else if (strcmp(command, "test") == 0) {
        start_test();
    } else if (strcmp(command, "stop") == 0) {
        stop_test();
    } else if (strcmp(command, "pi") == 0) {
        write_pi_gains();
    } else if (strcmp(command, "lpd") == 0) {
        write_line_gains();
    } else if (strncmp(command, "lpd ", 4U) == 0) {
        const char *cursor = command + 4U;
        int32_t kp;
        int32_t kd;
        if (parse_i32(&cursor, &kp) && parse_i32(&cursor, &kd) &&
            (kp >= 0) && (kd >= 0)) {
            app_line_control_set_gains((uint16_t) kp, (uint16_t) kd);
            write_line_gains();
        } else {
            bsp_uart_write("ERR use: lpd <Kp10000> <Kd10000>\r\n");
        }
    } else if (strncmp(command, "pi ", 3U) == 0) {
        const char *cursor = command + 3U;
        int32_t kp;
        int32_t ki;
        int32_t feedforward;
        if (parse_i32(&cursor, &kp) && parse_i32(&cursor, &ki) &&
            parse_i32(&cursor, &feedforward) && (kp >= 0) && (ki >= 0) &&
            (feedforward >= 0)) {
            app_speed_control_set_gains(
                (uint16_t) kp, (uint16_t) ki, (uint16_t) feedforward);
            write_pi_gains();
        } else {
            bsp_uart_write("ERR use: pi <Kp100> <Ki100> <FF100>\r\n");
        }
    } else if (strncmp(command, "speed ", 6U) == 0) {
        const char *cursor = command + 6U;
        int32_t left;
        int32_t right;
        if (parse_i32(&cursor, &left) && parse_i32(&cursor, &right) &&
            (left >= -30) && (left <= 30) &&
            (right >= -30) && (right <= 30)) {
            app_line_control_stop();
            bsp_encoder_reset();
            g_encoder = bsp_encoder_sample_window();
            app_speed_control_set_slew_step_x100(100U);
            app_speed_control_start((int16_t) left, (int16_t) right);
            g_phase = TEST_SPEED;
            g_phaseDeadline = bsp_timebase_now_ms() + 3000U;
            g_div10ms = 0;
            g_div200ms = 0;
            bsp_uart_write("OK speed control, 3s timeout\r\n");
        } else {
            bsp_uart_write("ERR speed targets must be -30..30 count/10ms\r\n");
        }
    } else if (strncmp(command, "follow ", 7U) == 0) {
        const char *cursor = command + 7U;
        int32_t baseSpeed;
        if (!parse_i32(&cursor, &baseSpeed) || (baseSpeed < 8) ||
            (baseSpeed > 20)) {
            bsp_uart_write("ERR follow speed must be 8..20 count/10ms\r\n");
        } else if (!bsp_gray_is_calibrated() || !g_line.valid) {
            bsp_uart_write("ERR line not valid; center sensor on line or calibrate\r\n");
        } else {
            bsp_encoder_reset();
            g_encoder = bsp_encoder_sample_window();
            (void) app_line_control_start((int16_t) baseSpeed);
            g_phase = TEST_FOLLOW;
            g_phaseDeadline = bsp_timebase_now_ms() + 2000U;
            g_div10ms = 0;
            g_div200ms = 0;
            bsp_uart_write("OK follow control, 2s timeout\r\n");
        }
    } else if (strncmp(command, "followrun ", 10U) == 0) {
        const char *cursor = command + 10U;
        int32_t baseSpeed;
        if (!parse_i32(&cursor, &baseSpeed) || (baseSpeed < 8) ||
            (baseSpeed > 20)) {
            bsp_uart_write("ERR followrun speed must be 8..20 count/10ms\r\n");
        } else if (!bsp_gray_is_calibrated() || !g_line.valid) {
            bsp_uart_write("ERR line not valid; center sensor on line or calibrate\r\n");
        } else {
            bsp_encoder_reset();
            g_encoder = bsp_encoder_sample_window();
            (void) app_line_control_start((int16_t) baseSpeed);
            g_phase = TEST_FOLLOW;
            g_phaseDeadline = bsp_timebase_now_ms() + 15000U;
            g_div10ms = 0;
            g_div200ms = 0;
            bsp_uart_write("OK followrun, line-loss stop, 15s guard timeout\r\n");
        }
    } else if (strncmp(command, "followarc ", 10U) == 0) {
        const char *cursor = command + 10U;
        int32_t baseSpeed;
        int32_t curveBias;
        if (!parse_i32(&cursor, &baseSpeed) ||
            !parse_i32(&cursor, &curveBias) ||
            (baseSpeed < 8) || (baseSpeed > 20) ||
            (curveBias < -6) || (curveBias > 6)) {
            bsp_uart_write("ERR use: followarc <8..20> <-6..6>\r\n");
        } else if (!bsp_gray_is_calibrated() || !g_line.valid) {
            bsp_uart_write("ERR line not valid; center sensor on line or calibrate\r\n");
        } else {
            bsp_encoder_reset();
            g_encoder = bsp_encoder_sample_window();
            (void) app_line_control_start_with_bias(
                (int16_t) baseSpeed, (int16_t) curveBias);
            g_phase = TEST_FOLLOW;
            g_phaseDeadline = bsp_timebase_now_ms() + 15000U;
            g_div10ms = 0;
            g_div200ms = 0;
            bsp_uart_write("OK followarc, line-loss stop, 15s guard timeout\r\n");
        }
    } else if (strncmp(command, "followfine ", 11U) == 0) {
        const char *cursor = command + 11U;
        int32_t baseSpeed;
        int32_t curveBias;
        if (!parse_i32(&cursor, &baseSpeed) ||
            !parse_i32(&cursor, &curveBias) ||
            (baseSpeed < 8) || (baseSpeed > 20) ||
            (curveBias < -6) || (curveBias > 6)) {
            bsp_uart_write("ERR use: followfine <8..20> <-6..6>\r\n");
        } else if (!bsp_gray_is_calibrated() || !g_line.valid) {
            bsp_uart_write("ERR line not valid; center sensor on line or calibrate\r\n");
        } else {
            bsp_encoder_reset();
            g_encoder = bsp_encoder_sample_window();
            if (app_attitude_state() == APP_ATTITUDE_READY) {
                app_attitude_zero_yaw();
            }
            (void) app_line_control_start_fine(
                (int16_t) baseSpeed, (int16_t) curveBias);
            g_phase = TEST_FOLLOW;
            g_phaseDeadline = bsp_timebase_now_ms() + 15000U;
            g_div10ms = 0;
            g_div200ms = 0;
            bsp_uart_write("OK followfine, line-loss stop, 15s guard timeout\r\n");
        }
    } else if (strncmp(command, "motor ", 6U) == 0) {
        const char *cursor = command + 6U;
        int32_t left;
        int32_t right;
        if (parse_i32(&cursor, &left) && parse_i32(&cursor, &right)) {
            app_line_control_stop();
            app_speed_control_stop();
            bsp_motor_set((int16_t) left, (int16_t) right);
            g_phase = TEST_MANUAL;
            g_phaseDeadline = bsp_timebase_now_ms() + 1000U;
            bsp_uart_write("OK motor command, 1s timeout\r\n");
        } else {
            bsp_uart_write("ERR use: motor <left> <right>\r\n");
        }
    } else if (command[0] != '\0') {
        bsp_uart_write("ERR unknown command\r\n");
    }
}

void app_bringup_init(void)
{
    g_phase = TEST_IDLE;
    g_phaseDeadline = 0;
    g_div10ms = 0;
    g_div5ms = 0;
    g_div500ms = 0;
    g_div200ms = 0;
    g_calibrationDeadline = 0;
    g_buttonCandidate = bsp_user_button_pressed();
    g_buttonStable = g_buttonCandidate;
    g_buttonCount = 0;
    g_encoder = bsp_encoder_sample_window();
    bsp_gray_scan();
    g_line = bsp_gray_line();
    app_speed_control_init();
    app_heading_control_init();
    app_line_control_init();
    app_attitude_init();
    app_route3_init();

    bsp_uart_write("\r\n2024hVibe bringup ready\r\n");
    bsp_uart_write("PB21 demo: route3 A-C-B-D-A, press again to stop.\r\n");
    if (bsp_imu_online()) {
        bsp_uart_write("ICM-45686 online; keep still for 2s calibration.\r\n");
    } else {
        bsp_uart_write("ICM-45686 offline; check 3V3/GND/CS/AD0/SCL/SDA.\r\n");
    }
    bsp_uart_write("Type help for commands.\r\n");
}

void app_bringup_poll(void)
{
    char command[96];

    if (bsp_uart_read_line(command, sizeof(command))) {
        process_command(command);
    }
}

void app_bringup_tick_1ms(void)
{
    uint32_t now = bsp_timebase_now_ms();
    bool button = bsp_user_button_pressed();

    if (button != g_buttonCandidate) {
        g_buttonCandidate = button;
        g_buttonCount = 0;
    } else if (g_buttonCount < 20U) {
        g_buttonCount++;
        if ((g_buttonCount == 20U) && (g_buttonStable != button)) {
            g_buttonStable = button;
            if (button) {
                if (g_phase == TEST_IDLE) {
                    start_button_route3_full();
                } else {
                    stop_test();
                }
            }
        }
    }

    if ((g_phase != TEST_IDLE) && deadline_reached(now, g_phaseDeadline)) {
        switch (g_phase) {
        case TEST_LEFT:
            bsp_motor_stop();
            g_phase = TEST_GAP;
            g_phaseDeadline = now + 500U;
            bsp_uart_write("TEST gap 0.5s\r\n");
            break;
        case TEST_GAP:
            bsp_motor_set(0, 220);
            g_phase = TEST_RIGHT;
            g_phaseDeadline = now + 1000U;
            bsp_uart_write("TEST right wheel 1s\r\n");
            break;
        case TEST_RIGHT:
        case TEST_MANUAL:
        case TEST_SPEED:
        case TEST_FOLLOW:
        default:
            stop_test();
            break;
        }
    }

    if (bsp_gray_calibration_active() &&
        deadline_reached(now, g_calibrationDeadline)) {
        finish_calibration();
    }

    g_div5ms++;
    if (g_div5ms >= 5U) {
        g_div5ms = 0;
        app_attitude_tick_5ms();
    }

    g_div10ms++;
    if (g_div10ms >= 10U) {
        g_div10ms = 0;
        g_encoder = bsp_encoder_sample_window();
        bsp_gray_scan();
        g_line = bsp_gray_line();
        if (g_phase == TEST_FOLLOW) {
            if (!app_line_control_update(
                    g_line, g_encoder.leftDelta, g_encoder.rightDelta)) {
                g_phase = TEST_IDLE;
                bsp_uart_write("FOLLOW lost line, stopped\r\n");
            }
        } else if ((g_phase == TEST_ROUTE3_AC) ||
                   (g_phase == TEST_ROUTE3_ACB) ||
                   (g_phase == TEST_ROUTE3_FULL) ||
                   (g_phase == TEST_ROUTE3_CB)) {
            AppRoute3Event event = app_route3_update_10ms(g_line, g_encoder);
            if (event == APP_ROUTE3_EVENT_C_REACHED) {
                g_phase = TEST_IDLE;
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 reached C, stopped\r\n");
            } else if (event == APP_ROUTE3_EVENT_C_PASSED) {
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 passed C, acquiring C-to-B arc\r\n");
            } else if (event == APP_ROUTE3_EVENT_DISTANCE_GUARD) {
                g_phase = TEST_IDLE;
                bsp_uart_write("ROUTE3 AC failed: distance guard\r\n");
            } else if (event == APP_ROUTE3_EVENT_B_REACHED) {
                g_phase = TEST_IDLE;
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 reached B, stopped\r\n");
            } else if (event == APP_ROUTE3_EVENT_B_PASSED) {
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 passed B, heading to D\r\n");
            } else if (event == APP_ROUTE3_EVENT_D_PASSED) {
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 passed D, acquiring D-to-A arc\r\n");
            } else if (event == APP_ROUTE3_EVENT_A_REACHED) {
                g_phase = TEST_IDLE;
                bsp_user_led_toggle();
                bsp_uart_write("ROUTE3 reached A, lap complete\r\n");
            } else if (event == APP_ROUTE3_EVENT_ACQUIRE_GUARD) {
                g_phase = TEST_IDLE;
                bsp_uart_write("ROUTE3 CB failed: arc acquire guard\r\n");
            } else if (event == APP_ROUTE3_EVENT_BD_DISTANCE_GUARD) {
                g_phase = TEST_IDLE;
                bsp_uart_write("ROUTE3 BD failed: distance guard\r\n");
            } else if (event == APP_ROUTE3_EVENT_D_ACQUIRE_GUARD) {
                g_phase = TEST_IDLE;
                bsp_uart_write("ROUTE3 DA failed: arc acquire guard\r\n");
            } else if (event == APP_ROUTE3_EVENT_TURN_GUARD) {
                g_phase = TEST_IDLE;
                bsp_uart_write("ROUTE3 failed: pivot turn timeout\r\n");
            }
        }
        app_speed_control_update(
            g_encoder.leftDelta, g_encoder.rightDelta);
    }

    g_div200ms++;
    if (g_div200ms >= 200U) {
        g_div200ms = 0;
        if (g_phase == TEST_SPEED) {
            write_speed_status();
        } else if (g_phase == TEST_FOLLOW) {
            write_follow_status();
        } else if ((g_phase == TEST_ROUTE3_AC) ||
                   (g_phase == TEST_ROUTE3_ACB) ||
                   (g_phase == TEST_ROUTE3_FULL) ||
                   (g_phase == TEST_ROUTE3_CB)) {
            write_route3_status();
        }
    }

    g_div500ms++;
    if (g_div500ms >= 500U) {
        g_div500ms = 0;
        bsp_user_led_toggle();
        if ((g_phase != TEST_SPEED) && (g_phase != TEST_FOLLOW)) {
            write_status();
        }
    }
}
