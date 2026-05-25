#ifndef __MOTOR_MSP_H__
#define __MOTOR_MSP_H__
#include <stdint.h>
void motor_init(void);
void motor_set(int16_t left, int16_t right);
void motor_brake(void);
#endif
