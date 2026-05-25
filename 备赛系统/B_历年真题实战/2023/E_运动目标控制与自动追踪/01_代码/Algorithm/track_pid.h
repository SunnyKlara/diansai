#ifndef __TRACK_PID_H__
#define __TRACK_PID_H__
#include <stdint.h>

typedef struct {
    float kp_x, kd_x;
    float kp_y, kd_y;
    float prev_ex, prev_ey;
} track_pid_t;

void track_init(track_pid_t *p);
void track_update(track_pid_t *p, int16_t cx, int16_t cy,
                  float *yaw_inc_deg, float *pitch_inc_deg);

#endif
