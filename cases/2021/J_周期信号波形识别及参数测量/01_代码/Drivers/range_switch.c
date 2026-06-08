#include "range_switch.h"

static range_t s_cur = RNG_MID;

void rng_init(void){ s_cur = RNG_MID; }
void rng_set(range_t r){ s_cur = r; /* 真机：写 GPIO 控 CD4053 SX/SY/SZ */ }
range_t rng_get(void){ return s_cur; }

range_t rng_auto(uint16_t adc_max, uint16_t adc_min)
{
    uint16_t vpp = (adc_max > adc_min) ? (adc_max - adc_min) : 0;
    if (vpp > (uint16_t)(0.80f * 4096.0f) && s_cur != RNG_HIGH) return RNG_HIGH;
    if (vpp < (uint16_t)(0.05f * 4096.0f) && s_cur != RNG_LOW)  return RNG_LOW;
    return s_cur;
}

float rng_gain(range_t r)
{
    switch (r) {
        case RNG_LOW:  return RNG_LOW_GAIN;
        case RNG_HIGH: return RNG_HIGH_GAIN;
        default:       return RNG_MID_GAIN;
    }
}
