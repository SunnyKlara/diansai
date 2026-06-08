#ifndef __MOTOR_H__
#define __MOTOR_H__
#include <stdint.h>
void motor_init(void);
void motor_set(int16_t left_pwm, int16_t right_pwm);
void motor_brake(void);
#endif
