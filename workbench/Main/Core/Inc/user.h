#ifndef __USER_H
#define __USER_H

#include "stm32h7xx_hal.h"

uint8_t KEY_read(void);
void KEY_Process(void);
void fan_pwm(void);

#endif // __USER_H
