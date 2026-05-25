#ifndef __PWM_COIL_H__
#define __PWM_COIL_H__
#include <stdint.h>
void pwm_coil_init(void);
void pwm_coil_start(void);
void pwm_coil_stop(void);
void pwm_coil_set(uint8_t coil_idx, float duty);
void pwm_coil_emergency_off(void);
#endif
