#include "maglev_pd.h"

void maglev_pd_init(maglev_pd_t *p, float kp, float kd, float ki)
{
    if (!p) return;
    p->kp = kp; p->kd = kd; p->ki = ki;
    p->integ = 0; p->prev_meas = 20.0f;
    p->out_min = 0.0f; p->out_max = 0.95f;
}

void maglev_pd_reset(maglev_pd_t *p)
{
    if (!p) return;
    p->integ = 0; p->prev_meas = 20.0f;
}

float maglev_pd_update(maglev_pd_t *p, float setpoint, float meas, float dt)
{
    if (!p) return 0;
    float err = setpoint - meas;
    float p_term = p->kp * err;
    /* 微分先行：对测量值求导，避免设定值突变冲击 */
    float d_meas = (meas - p->prev_meas) / dt;
    float d_term = -p->kd * d_meas;
    p->prev_meas = meas;
    /* 积分小但限幅 */
    p->integ += p->ki * err * dt;
    if (p->integ > p->out_max * 0.3f) p->integ = p->out_max * 0.3f;
    if (p->integ < 0)                 p->integ = 0;
    float u = p_term + d_term + p->integ;
    if (u < p->out_min) u = p->out_min;
    if (u > p->out_max) u = p->out_max;
    return u;
}
