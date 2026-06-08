#ifndef __LINE_PID_H__
#define __LINE_PID_H__
#include <stdint.h>

typedef struct {
    float kp; float ki; float kd;
    float prev_err;
    float integ;
} line_pid_t;

void line_pid_init(line_pid_t *p);
float line_pid_update(line_pid_t *p, float err);
void line_pid_reset(line_pid_t *p);

#endif
