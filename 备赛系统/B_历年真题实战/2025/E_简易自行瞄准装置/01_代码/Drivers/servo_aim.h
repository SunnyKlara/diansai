#ifndef __SERVO_AIM_H__
#define __SERVO_AIM_H__
#include <stdint.h>
void servo_init(void);
void servo_set(float yaw_deg, float pitch_deg);
void servo_get(float *yaw_deg, float *pitch_deg);
#endif
