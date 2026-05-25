#ifndef __SERVO_GIMBAL_H__
#define __SERVO_GIMBAL_H__
#include <stdint.h>
void gimbal_init(void);
void gimbal_set(float yaw_deg, float pitch_deg);
void gimbal_get(float *yaw_deg, float *pitch_deg);
#endif
