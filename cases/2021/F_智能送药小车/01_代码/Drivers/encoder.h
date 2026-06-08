#ifndef __ENCODER_H__
#define __ENCODER_H__
#include <stdint.h>
void enc_init(void);
int32_t enc_get(uint8_t side);          /* 0=L, 1=R */
void enc_reset(uint8_t side);
void enc_on_isr(uint8_t side, int32_t delta);
#endif
