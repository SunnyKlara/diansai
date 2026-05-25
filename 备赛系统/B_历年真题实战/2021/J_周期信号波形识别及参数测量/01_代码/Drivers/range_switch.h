#ifndef __RANGE_SWITCH_H__
#define __RANGE_SWITCH_H__
#include "../config.h"
void rng_init(void);
void rng_set(range_t r);
range_t rng_get(void);
range_t rng_auto(uint16_t adc_max, uint16_t adc_min);
float rng_gain(range_t r);
#endif
