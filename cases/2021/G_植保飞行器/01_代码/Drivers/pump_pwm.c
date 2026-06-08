#include "pump_pwm.h"
static volatile uint8_t s_active = 0;
void pump_init(void){ s_active = 0; }
void pump_set(uint8_t enable){ s_active = enable; /* GPIO 高/低控继电器 */ }
uint8_t pump_get(void){ return s_active; }
