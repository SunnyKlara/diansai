#ifndef __MOTOR_H__
#define __MOTOR_H__
#include <stdint.h>
void motor_init(void);
void motor_set(int16_t l, int16_t r);
void motor_brake(void);
#endif
