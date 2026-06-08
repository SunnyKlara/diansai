#include "user.h"

u32 fan_pwm_tick = 0;
void fan_pwm()
{
	if(uwTick - fan_pwm_tick < 2)
		return;
	fan_pwm_tick = uwTick;
}





