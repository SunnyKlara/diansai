#include "BSP/bsp_motor.h"

#include "ti_msp_dl_config.h"

#define MOTOR_BRINGUP_LIMIT_PERMILLE (400)

static int16_t g_leftCommand;
static int16_t g_rightCommand;

static int16_t clamp_command(int16_t command)
{
    if (command > MOTOR_BRINGUP_LIMIT_PERMILLE) {
        return MOTOR_BRINGUP_LIMIT_PERMILLE;
    }
    if (command < -MOTOR_BRINGUP_LIMIT_PERMILLE) {
        return -MOTOR_BRINGUP_LIMIT_PERMILLE;
    }
    return command;
}

static void set_direction(uint32_t in1, uint32_t in2, int16_t command)
{
    if (command > 0) {
        DL_GPIO_setPins(GPIO_MOTOR_PORT, in1);
        DL_GPIO_clearPins(GPIO_MOTOR_PORT, in2);
    } else if (command < 0) {
        DL_GPIO_clearPins(GPIO_MOTOR_PORT, in1);
        DL_GPIO_setPins(GPIO_MOTOR_PORT, in2);
    } else {
        DL_GPIO_clearPins(GPIO_MOTOR_PORT, in1 | in2);
    }
}

static uint32_t command_to_compare(int16_t command)
{
    uint32_t magnitude = (command < 0) ? (uint32_t) (-command) :
                                         (uint32_t) command;
    uint32_t period = 3200U;

    return period - ((period * magnitude) / 1000U);
}

void bsp_motor_init(void)
{
    bsp_motor_stop();
}

void bsp_motor_set(int16_t leftPermille, int16_t rightPermille)
{
    leftPermille = clamp_command(leftPermille);
    rightPermille = clamp_command(rightPermille);

    set_direction(GPIO_MOTOR_AIN1_PIN, GPIO_MOTOR_AIN2_PIN, leftPermille);
    set_direction(GPIO_MOTOR_BIN1_PIN, GPIO_MOTOR_BIN2_PIN, rightPermille);

    DL_TimerA_setCaptureCompareValue(MOTOR_PWM_INST,
        command_to_compare(leftPermille), GPIO_MOTOR_PWM_C1_IDX);
    DL_TimerA_setCaptureCompareValue(MOTOR_PWM_INST,
        command_to_compare(rightPermille), GPIO_MOTOR_PWM_C2_IDX);

    g_leftCommand = leftPermille;
    g_rightCommand = rightPermille;

    if ((leftPermille == 0) && (rightPermille == 0)) {
        DL_GPIO_clearPins(GPIO_MOTOR_PORT, GPIO_MOTOR_STBY_PIN);
    } else {
        DL_GPIO_setPins(GPIO_MOTOR_PORT, GPIO_MOTOR_STBY_PIN);
    }
}

void bsp_motor_stop(void)
{
    DL_GPIO_clearPins(GPIO_MOTOR_PORT, GPIO_MOTOR_STBY_PIN);
    DL_TimerA_setCaptureCompareValue(
        MOTOR_PWM_INST, 3200U, GPIO_MOTOR_PWM_C1_IDX);
    DL_TimerA_setCaptureCompareValue(
        MOTOR_PWM_INST, 3200U, GPIO_MOTOR_PWM_C2_IDX);
    DL_GPIO_clearPins(GPIO_MOTOR_PORT, GPIO_MOTOR_AIN1_PIN |
        GPIO_MOTOR_AIN2_PIN | GPIO_MOTOR_BIN1_PIN |
        GPIO_MOTOR_BIN2_PIN);
    g_leftCommand = 0;
    g_rightCommand = 0;
}

int16_t bsp_motor_left_command(void)
{
    return g_leftCommand;
}

int16_t bsp_motor_right_command(void)
{
    return g_rightCommand;
}
