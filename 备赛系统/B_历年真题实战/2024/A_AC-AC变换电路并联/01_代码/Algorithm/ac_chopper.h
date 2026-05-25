/**
 * @file    ac_chopper.h
 * @brief   AC-AC 斩波器算法层（占空比生成 + 四步换流序列）
 *          算法层，仅依赖 stdint / config.h，不引 HAL。
 */

#ifndef __AC_CHOPPER_H__
#define __AC_CHOPPER_H__

#include <stdint.h>

typedef enum {
    CHOP_HALF_POSITIVE = 0,   /* 正半周，S1 组斩波 */
    CHOP_HALF_NEGATIVE = 1    /* 负半周，S2 组斩波 */
} chop_half_t;

/**
 * 4 路 PWM 占空比（CCR1..CCR4 ÷ ARR）
 */
typedef struct {
    float ccr1; float ccr2; float ccr3; float ccr4;
} chop_pwm_t;

/**
 * @brief 由当前半周 + 占空比生成 4 路 PWM 输出
 */
void chop_compute_pwm(chop_half_t half, float duty, chop_pwm_t *out);

/**
 * @brief 过零换流（半周切换时）：执行 4 步换流 PWM 时序
 *        返回每步的目标 PWM，由驱动层按 1μs 间隔写入。
 *
 * @param[in]  step  0..3
 * @param[in]  next  下一个半周
 * @param[in]  duty  目标占空比
 * @param[out] out   该步的 PWM 值
 */
void chop_step_commutation(uint8_t step, chop_half_t next,
                           float duty, chop_pwm_t *out);

#endif
