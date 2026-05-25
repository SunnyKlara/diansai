#include "adc_dual.h"
#include "../config.h"
#include <string.h>
static uint16_t s_buf_a[IMPEDANCE_FRAME_LEN];
static uint16_t s_buf_b[IMPEDANCE_FRAME_LEN];
static volatile uint8_t s_ready = 0;

void adc_dual_init(void){ s_ready = 0; }
void adc_dual_start(void){}
uint8_t adc_dual_get(uint16_t *a, uint16_t *b, uint32_t len)
{
    if (!a || !b || !s_ready || len < IMPEDANCE_FRAME_LEN) return 0;
    memcpy(a, s_buf_a, IMPEDANCE_FRAME_LEN * sizeof(uint16_t));
    memcpy(b, s_buf_b, IMPEDANCE_FRAME_LEN * sizeof(uint16_t));
    s_ready = 0;
    return 1;
}
void adc_dual_on_dma(const uint16_t *a, const uint16_t *b, uint32_t n)
{
    if (!a || !b) return;
    uint32_t k = (n > IMPEDANCE_FRAME_LEN) ? IMPEDANCE_FRAME_LEN : n;
    memcpy(s_buf_a, a, k * sizeof(uint16_t));
    memcpy(s_buf_b, b, k * sizeof(uint16_t));
    s_ready = 1;
}
