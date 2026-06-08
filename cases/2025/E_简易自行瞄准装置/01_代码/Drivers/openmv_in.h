#ifndef __OPENMV_IN_H__
#define __OPENMV_IN_H__
#include <stdint.h>
void omv_init(void);
void omv_on_byte(uint8_t b);
uint8_t omv_get(int16_t *cx, int16_t *cy);
#endif
