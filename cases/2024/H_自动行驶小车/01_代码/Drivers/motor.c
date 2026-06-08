/**
 * @file    motor.c
 * @brief   MSPM0G3507 + TB6612 电机驱动 —— 真实 driverlib 实现
 *
 * 资源映射（与 引脚分配表.md / 接线表.md 同步）：
 *   PWM 通道：TIMA0_C2(左) / TIMA0_C3(右)，5kHz Edge-Aligned
 *   方向位  ：PB6/PB7  AIN1/AIN2 (左)
 *           PB16/PB17 BIN1/BIN2 (右)
 *   STBY    ：PB18 (高=工作 低=休眠)
 *
 * 编译说明：本文件依赖 SysConfig 自动生成的 ti_msp_dl_config.{c,h}，
 *           其中外设句柄 PWM_0_INST、GPIO_GRP_MOTOR_DIR_*_PORT 等已定义。
 */

#include "motor.h"
#include "../config.h"
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>
#include "ti_msp_dl_config.h"

/* ========================================================================
 * 内部辅助
 * ===================================================================== */

static int16_t clamp_pwm(int16_t v)
{
    if (v >  MOTOR_PWM_MAX) v =  MOTOR_PWM_MAX;
    if (v < -MOTOR_PWM_MAX) v = -MOTOR_PWM_MAX;
    return v;
}

/* SysConfig 生成的宏（示例命名，按实际调整）：
 *   GPIO_GRP_MOTOR_DIR_PORT, _AIN1_PIN, _AIN2_PIN, _BIN1_PIN, _BIN2_PIN
 *   GPIO_GRP_STBY_PORT, _STBY_PIN
 *   PWM_0_INST            ← TIMA0
 *   GPIO_PWM_0_C2_IOMUX   ← PB0
 *   GPIO_PWM_0_C3_IOMUX   ← PB1
 */

static inline void dir_left(int sign)
{
    if (sign > 0) {
        DL_GPIO_setPins  (GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_AIN1_PIN);
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_AIN2_PIN);
    } else if (sign < 0) {
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_AIN1_PIN);
        DL_GPIO_setPins  (GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_AIN2_PIN);
    } else {
        /* 滑行（双零）*/
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT,
                          GPIO_GRP_MOTOR_DIR_AIN1_PIN | GPIO_GRP_MOTOR_DIR_AIN2_PIN);
    }
}

static inline void dir_right(int sign)
{
    if (sign > 0) {
        DL_GPIO_setPins  (GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_BIN1_PIN);
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_BIN2_PIN);
    } else if (sign < 0) {
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_BIN1_PIN);
        DL_GPIO_setPins  (GPIO_GRP_MOTOR_DIR_PORT, GPIO_GRP_MOTOR_DIR_BIN2_PIN);
    } else {
        DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT,
                          GPIO_GRP_MOTOR_DIR_BIN1_PIN | GPIO_GRP_MOTOR_DIR_BIN2_PIN);
    }
}

/* ========================================================================
 * API
 * ===================================================================== */

void Motor_Init(void)
{
    /* 1. 方向位与 STBY 已经在 SYSCFG_DL_init() 配置为 OUTPUT，初值 0 */
    /* 2. 第一时间把所有方向位拉低 + PWM 输出 0 */
    DL_GPIO_clearPins(GPIO_GRP_MOTOR_DIR_PORT,
                      GPIO_GRP_MOTOR_DIR_AIN1_PIN | GPIO_GRP_MOTOR_DIR_AIN2_PIN |
                      GPIO_GRP_MOTOR_DIR_BIN1_PIN | GPIO_GRP_MOTOR_DIR_BIN2_PIN);

    DL_TimerA_setCaptureCompareValue(PWM_0_INST, 0, DL_TIMER_CC_2_INDEX);
    DL_TimerA_setCaptureCompareValue(PWM_0_INST, 0, DL_TIMER_CC_3_INDEX);

    /* 3. 启动 PWM 计数器 */
    DL_TimerA_startCounter(PWM_0_INST);

    /* 4. 拉高 STBY（最后一步，之前 PWM 必须为 0 防冲出）*/
    DL_GPIO_setPins(GPIO_GRP_STBY_PORT, GPIO_GRP_STBY_STBY_PIN);
}

static void apply_left(int16_t pwm)
{
    pwm = clamp_pwm(pwm);
    int sign = (pwm > 0) ? 1 : (pwm < 0) ? -1 : 0;
    dir_left(sign);

    uint16_t mag = (uint16_t)((pwm < 0) ? -pwm : pwm);
    DL_TimerA_setCaptureCompareValue(PWM_0_INST, mag, DL_TIMER_CC_2_INDEX);
}

static void apply_right(int16_t pwm)
{
    pwm = clamp_pwm(pwm);
    int sign = (pwm > 0) ? 1 : (pwm < 0) ? -1 : 0;
    dir_right(sign);

    uint16_t mag = (uint16_t)((pwm < 0) ? -pwm : pwm);
    DL_TimerA_setCaptureCompareValue(PWM_0_INST, mag, DL_TIMER_CC_3_INDEX);
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
    /* 滑行停止：双零 + PWM 0 */
    apply_left(0);
    apply_right(0);
}

void Motor_Brake(void)
{
    /* 短路制动：IN1=IN2=1 + PWM 0 */
    DL_GPIO_setPins(GPIO_GRP_MOTOR_DIR_PORT,
                    GPIO_GRP_MOTOR_DIR_AIN1_PIN | GPIO_GRP_MOTOR_DIR_AIN2_PIN |
                    GPIO_GRP_MOTOR_DIR_BIN1_PIN | GPIO_GRP_MOTOR_DIR_BIN2_PIN);
    DL_TimerA_setCaptureCompareValue(PWM_0_INST, 0, DL_TIMER_CC_2_INDEX);
    DL_TimerA_setCaptureCompareValue(PWM_0_INST, 0, DL_TIMER_CC_3_INDEX);
}

void Motor_EmergencyOff(void)
{
    /* 立即关 STBY（PB18 = 0），TB6612 内部全部断开，比 Motor_Stop 更快 */
    DL_GPIO_clearPins(GPIO_GRP_STBY_PORT, GPIO_GRP_STBY_STBY_PIN);
    Motor_Stop();
}
