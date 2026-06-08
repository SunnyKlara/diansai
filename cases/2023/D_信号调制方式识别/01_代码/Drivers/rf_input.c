#include "rf_input.h"
#include "../config.h"
#include <string.h>

static float s_i_buf[MOD_FRAME_LEN], s_q_buf[MOD_FRAME_LEN];
static volatile uint8_t s_ready = 0;

void rf_init(void){ s_ready = 0; }
uint8_t rf_get_iq(float *i, float *q, uint32_t len)
{
    if (!i || !q || !s_ready || len < MOD_FRAME_LEN) return 0;
    memcpy(i, s_i_buf, MOD_FRAME_LEN * sizeof(float));
    memcpy(q, s_q_buf, MOD_FRAME_LEN * sizeof(float));
    s_ready = 0;
    return 1;
}
