#include "motor_msp.h"
static volatile int16_t s_pwm[2] = {0, 0};
void motor_init(void) {}
void motor_set(int16_t l, int16_t r){ s_pwm[0]=l; s_pwm[1]=r; }
void motor_brake(void){ s_pwm[0]=s_pwm[1]=0; }
