#ifndef __DIST_PID_H__
#define __DIST_PID_H__
#include <stdint.h>

typedef struct {
    float kp, ki, kd;
    float prev_err;
    float integ;
    float v_base_mps;
} dist_pid_t;

void dist_pid_init(dist_pid_t *p);
void dist_pid_set_base_speed(dist_pid_t *p, float v_base);
float dist_pid_update(dist_pid_t *p, float dist_cm);   /* 返回目标速度 m/s */
#endif
