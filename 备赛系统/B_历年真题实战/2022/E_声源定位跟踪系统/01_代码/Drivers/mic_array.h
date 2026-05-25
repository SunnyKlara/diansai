#ifndef __MIC_ARRAY_H__
#define __MIC_ARRAY_H__
#include <stdint.h>
#include "../config.h"
void mic_init(void);
void mic_start(void);
uint8_t mic_get_frame(int16_t *m1, int16_t *m2, int16_t *m3, int16_t *m4, uint32_t len);
#endif
