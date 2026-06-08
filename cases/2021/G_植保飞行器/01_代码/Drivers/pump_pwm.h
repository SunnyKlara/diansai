#ifndef __PUMP_PWM_H__
#define __PUMP_PWM_H__
#include <stdint.h>
void pump_init(void);
void pump_set(uint8_t enable);
uint8_t pump_get(void);
#endif
