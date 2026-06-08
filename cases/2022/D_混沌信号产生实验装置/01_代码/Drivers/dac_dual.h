#ifndef __DAC_DUAL_H__
#define __DAC_DUAL_H__
#include <stdint.h>
void dac_dual_init(void);
void dac_dual_set(uint16_t v_x, uint16_t v_y);
#endif
