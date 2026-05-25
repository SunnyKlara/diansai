#include "pair_mux.h"
static uint8_t s_cur = 0;
void mux_init(void){ s_cur = 0; }
void mux_select(uint8_t pair_idx){ s_cur = pair_idx & 3; /* GPIO 控 CD4051 */ }
