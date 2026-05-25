#include "encoder.h"
static volatile int32_t s_cnt[2] = {0, 0};
void enc_init(void) {}
int32_t enc_get(uint8_t side){ return (side<2) ? s_cnt[side] : 0; }
void enc_reset(uint8_t side){ if (side<2) s_cnt[side] = 0; }
void enc_on_isr(uint8_t side, int32_t delta){ if (side<2) s_cnt[side] += delta; }
