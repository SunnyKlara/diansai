#ifndef __TDR_CAPTURE_H__
#define __TDR_CAPTURE_H__
#include <stdint.h>
#include "../config.h"
void tdr_init(void);
void tdr_trigger(void);
uint8_t tdr_collect(float *wave_out, uint32_t len);
#endif
