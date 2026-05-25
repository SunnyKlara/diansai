#include "motor.h"
static volatile int16_t s_pwm[2] = {0, 0};
void motor_init(void) {}
void motor_set(int16_t left, int16_t right) { s_pwm[0]=left; s_pwm[1]=right; }
void motor_brake(void) { s_pwm[0]=s_pwm[1]=0; }
