#include "line_pid.h"
#include "../config.h"

void line_pid_init(line_pid_t *p)
{
    if (!p) return;
    p->kp = LINE_KP; p->kd = LINE_KD; p->ki = LINE_KI;
    p->prev_err = 0.0f; p->integ = 0.0f;
}

float line_pid_update(line_pid_t *p, float err)
{
    if (!p) return 0.0f;
    float de = err - p->prev_err;
    p->prev_err = err;
    p->integ += err;
    if (p->integ >  500.0f) p->integ =  500.0f;
    if (p->integ < -500.0f) p->integ = -500.0f;
    return p->kp * err + p->kd * de + p->ki * p->integ;
}

void line_pid_reset(line_pid_t *p)
{
    if (!p) return;
    p->prev_err = 0.0f; p->integ = 0.0f;
}
