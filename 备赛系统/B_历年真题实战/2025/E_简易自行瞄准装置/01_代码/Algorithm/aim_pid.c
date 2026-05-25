#include "aim_pid.h"
#include "../config.h"
static float clamp(float x, float lo, float hi){ if(x<lo)return lo;if(x>hi)return hi;return x; }

void aim_init(aim_pid_t *p)
{
    if (!p) return;
    p->kp_x = AIM_KP_X; p->kp_y = AIM_KP_Y;
    p->kd_x = AIM_KD_X; p->kd_y = AIM_KD_Y;
    p->prev_ex = 0; p->prev_ey = 0;
}
void aim_update(aim_pid_t *p, int16_t cx, int16_t cy, float *dyaw, float *dpit)
{
    if (!p || !dyaw || !dpit) return;
    float ex = (float)(cx - IMG_CX);
    float ey = (float)(cy - IMG_CY);
    float dex = ex - p->prev_ex; p->prev_ex = ex;
    float dey = ey - p->prev_ey; p->prev_ey = ey;
    *dyaw = clamp(p->kp_x * ex + p->kd_x * dex, -3.0f, 3.0f);
    *dpit = clamp(p->kp_y * ey + p->kd_y * dey, -3.0f, 3.0f);
}
