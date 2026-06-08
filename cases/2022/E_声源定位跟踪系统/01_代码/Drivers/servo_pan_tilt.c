#include "servo_pan_tilt.h"
static volatile float s_yaw = 0, s_pit = 0;
void servo_init(void){}
void servo_set_target(float yaw_deg, float pit_deg){ s_yaw = yaw_deg; s_pit = pit_deg; }
