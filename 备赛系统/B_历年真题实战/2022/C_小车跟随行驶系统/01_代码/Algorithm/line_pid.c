#include "line_pid.h"
#include "../config.h"
void line_pid_init(line_pid_t *p){ if (!p) return; p->kp=LINE_KP; p->kd=LINE_KD; p->prev_err=0; }
float line_pid_update(line_pid_t *p, float err)
{
    if (!p) return 0;
    float de = err - p->prev_err; p->prev_err = err;
    return p->kp * err + p->kd * de;
}
void line_pid_reset(line_pid_t *p){ if (p) p->prev_err=0; }
