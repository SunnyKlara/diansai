/**
 * @file    pwm_chopper.h
 * @brief   HRTIM 4 通道 PWM 驱动 + 四步换流时序
 */
#ifndef __PWM_CHOPPER_H__
#define __PWM_CHOPPER_H__

#include <stdint.h>
#include "../Algorithm/ac_chopper.h"

void pwm_chopper_init(void);
void pwm_chopper_start(void);
void pwm_chopper_stop(void);
void pwm_chopper_set(const chop_pwm_t *p);
void pwm_chopper_emergency_off(void);

#endif
