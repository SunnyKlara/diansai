#ifndef __RF_INPUT_H__
#define __RF_INPUT_H__
#include <stdint.h>
void rf_init(void);
uint8_t rf_get_iq(float *i_buf, float *q_buf, uint32_t len);
#endif
