#ifndef __AIM_PID_H__
#define __AIM_PID_H__
#include <stdint.h>
typedef struct {
    float kp_x, kp_y, kd_x, kd_y;
    float prev_ex, prev_ey;
} aim_pid_t;
void aim_init(aim_pid_t *p);
void aim_update(aim_pid_t *p, int16_t cx, int16_t cy, float *dyaw, float *dpit);
#endif
