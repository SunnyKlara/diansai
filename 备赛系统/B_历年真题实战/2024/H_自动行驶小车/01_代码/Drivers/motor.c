/**
 * @file    motor.c
 * @brief   MSPM0 + TB6612 电机驱动实现
 * @note    当前为 driverlib 占位实现，函数体标 TODO_MSPM0 处需在 SysConfig
 *          完成 PWM/GPIO 配置后用真实 API 替换。
 *
 *  PWM 通道：TIMA0_C2(左) / TIMA0_C3(右)，频率 50kHz，分辨率 1000
 *  方向引脚：PB0/PB1 (左 IN1/IN2)，PB2/PB3 (右 IN1/IN2)
 *  STBY 引脚：PB4
 */

#include "motor.h"
#include "../config.h"

/* TODO_MSPM0: 包含 ti/driverlib/driverlib.h 等头文件 */
/* #include <ti/driverlib/driverlib.h> */

/* ---- 私有：限幅 ---- */
static int16_t clamp_pwm(int16_t v)
{
    if (v >  MOTOR_PWM_MAX) v =  MOTOR_PWM_MAX;
    if (v < -MOTOR_PWM_MAX) v = -MOTOR_PWM_MAX;
    return v;
}

void Motor_Init(void)
{
    /* TODO_MSPM0:
     *  1) GPIO 输出：PB0..PB4 推挽输出，初始全 0
     *  2) PWM：TIMA0 配置为 PWM Edge-Aligned，CCP2/CCP3 输出，
     *          周期 = (80MHz / 50kHz) - 1 = 1599
     *          初始占空比 0
     *  3) STBY 拉高（PB4 = 1）
     */
}

static void apply_left(int16_t pwm)
{
    pwm = clamp_pwm(pwm);
    if (pwm >= 0) {
        /* 正转：AIN1=1, AIN2=0 */
        /* DL_GPIO_setPins(GPIOB, GPIO_PIN_0); */
        /* DL_GPIO_clearPins(GPIOB, GPIO_PIN_1); */
    } else {
        /* DL_GPIO_clearPins(GPIOB, GPIO_PIN_0); */
        /* DL_GPIO_setPins(GPIOB, GPIO_PIN_1); */
        pwm = -pwm;
    }
    /* DL_TimerA_setCaptureCompareValue(TIMA0, pwm, DL_TIMER_CC_2_INDEX); */
    (void)pwm;
}

static void apply_right(int16_t pwm)
{
    pwm = clamp_pwm(pwm);
    if (pwm >= 0) {
        /* DL_GPIO_setPins(GPIOB, GPIO_PIN_2); */
        /* DL_GPIO_clearPins(GPIOB, GPIO_PIN_3); */
    } else {
        /* DL_GPIO_clearPins(GPIOB, GPIO_PIN_2); */
        /* DL_GPIO_setPins(GPIOB, GPIO_PIN_3); */
        pwm = -pwm;
    }
    /* DL_TimerA_setCaptureCompareValue(TIMA0, pwm, DL_TIMER_CC_3_INDEX); */
    (void)pwm;
}

void Motor_SetLeft(int16_t pwm)   { apply_left(pwm); }
void Motor_SetRight(int16_t pwm)  { apply_right(pwm); }

void Motor_SetBoth(int16_t left_pwm, int16_t right_pwm)
{
    apply_left(left_pwm);
    apply_right(right_pwm);
}

void Motor_Stop(void)
{
    apply_left(0);
    apply_right(0);
    /* TODO_MSPM0: 拉低所有方向引脚（滑行停车）
     * DL_GPIO_clearPins(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3);
     */
}

void Motor_Brake(void)
{
    /* TB6612 短路制动：IN1=IN2=1，PWM=任意（实际不影响，TB6612 内部短路两端）*/
    /* DL_GPIO_setPins(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3); */
    /* DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_2_INDEX); */
    /* DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_3_INDEX); */
}
