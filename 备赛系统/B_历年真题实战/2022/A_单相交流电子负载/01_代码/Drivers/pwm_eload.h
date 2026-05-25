#ifndef __PWM_ELOAD_H__
#define __PWM_ELOAD_H__
#include <stdint.h>
void pwm_init(void);
void pwm_rectifier_start(void);  /* 前级 PWM 整流 */
void pwm_rectifier_stop(void);
void pwm_rectifier_set(float duty);
void pwm_inverter_start(void);   /* 后级回馈逆变 */
void pwm_inverter_stop(void);
void pwm_inverter_set(float duty);
void pwm_emergency_off(void);
#endif
