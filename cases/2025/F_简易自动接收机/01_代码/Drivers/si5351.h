#ifndef __SI5351_H__
#define __SI5351_H__
#include <stdint.h>
void si5351_init(void);
void si5351_set_freq(uint8_t channel, uint32_t hz);
#endif
