#include "pwm_eload.h"
#include "../config.h"

static volatile uint8_t s_r = 0, s_i = 0;

void pwm_init(void) {}
void pwm_rectifier_start(void){ s_r = 1; }
void pwm_rectifier_stop(void) { s_r = 0; }
void pwm_rectifier_set(float duty){ if (!s_r) return; (void)duty; }
void pwm_inverter_start(void) { s_i = 1; }
void pwm_inverter_stop(void)  { s_i = 0; }
void pwm_inverter_set(float duty){ if (!s_i) return; (void)duty; }
void pwm_emergency_off(void)  { s_r = s_i = 0; }
