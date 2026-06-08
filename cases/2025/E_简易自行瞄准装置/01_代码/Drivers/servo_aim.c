#include "servo_aim.h"
#include "../config.h"
static volatile float s_y=0, s_p=0;
void servo_init(void){ s_y=0; s_p=0; }
void servo_set(float y, float p)
{
    if (y<SERVO_YAW_MIN_DEG) y=SERVO_YAW_MIN_DEG;
    if (y>SERVO_YAW_MAX_DEG) y=SERVO_YAW_MAX_DEG;
    if (p<SERVO_PITCH_MIN_DEG) p=SERVO_PITCH_MIN_DEG;
    if (p>SERVO_PITCH_MAX_DEG) p=SERVO_PITCH_MAX_DEG;
    s_y=y; s_p=p;
}
void servo_get(float *y, float *p){ if(y)*y=s_y; if(p)*p=s_p; }
