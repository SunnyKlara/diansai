#ifndef __ULTRASONIC_H__
#define __ULTRASONIC_H__
#include <stdint.h>
void ultra_init(void);
void ultra_trigger(void);
void ultra_on_echo_isr(uint32_t pulse_us);
float ultra_get_cm(void);
#endif
