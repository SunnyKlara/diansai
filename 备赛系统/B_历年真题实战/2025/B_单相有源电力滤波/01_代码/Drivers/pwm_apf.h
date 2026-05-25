/**
 * @file    pwm_apf.h
 * @brief   APF H 桥互补 PWM 启停 + 占空比设置（STM32F407 TIM1）
 *
 * 资源映射（占位）：
 *   - TIM1 CH1/CH1N → MOSFET A 上下管（互补 + 死区 500ns）
 *   - TIM1 CH2/CH2N → MOSFET B 上下管（与 CH1 相位反向 = 单相 H 桥）
 *   - 频率 20 kHz, 中心对齐 PWM
 */

#ifndef __PWM_APF_H__
#define __PWM_APF_H__

#include <stdint.h>

/**
 * @brief 初始化 TIM1 中心对齐 PWM + 死区设置
 *        必须在 GPIO 配置后调用一次。
 */
void pwm_apf_init(void);

/**
 * @brief 启动 PWM 输出（使能 OC + MOE）。
 */
void pwm_apf_start(void);

/**
 * @brief 停止 PWM 输出（清除 MOE，所有管子关断）。
 *        故障保护 / APF_OFF / 上电预充时调用。
 */
void pwm_apf_stop(void);

/**
 * @brief 设置 H 桥占空比（单极性 PWM 模式）
 * @param duty  [0.0 .. 1.0]，0.5 为输出 0V，>0.5 输出正向，<0.5 输出反向
 *
 * 内部映射：
 *   CH1 占空比 = duty
 *   CH2 占空比 = 1 - duty
 */
void pwm_apf_set_duty(float duty);

/**
 * @brief 紧急关断（仅在故障 ISR 内调用，不阻塞）
 */
void pwm_apf_emergency_off(void);

#endif /* __PWM_APF_H__ */
