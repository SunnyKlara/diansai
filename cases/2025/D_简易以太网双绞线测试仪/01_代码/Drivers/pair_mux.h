#ifndef __PAIR_MUX_H__
#define __PAIR_MUX_H__
#include <stdint.h>
void mux_init(void);
void mux_select(uint8_t pair_idx);  /* 0..3 选择 4 对线 */
#endif
