#include "adc_load.h"
#include "../config.h"

static volatile load_sample_t s_smp;
static volatile uint8_t s_new = 0;

void adc_load_init(void) { s_new = 0; }
void adc_load_start(void) {}
uint8_t adc_load_get(load_sample_t *out)
{
    if (!out || !s_new) return 0;
    *out = s_smp; s_new = 0; return 1;
}
void adc_load_on_isr(uint16_t v_raw, uint16_t i_raw, uint16_t vb, uint16_t vo)
{
    s_smp.v_grid = ((float)v_raw - V_GRID_OFFSET) * V_GRID_K;
    s_smp.i_grid = ((float)i_raw - I_GRID_OFFSET) * I_GRID_K;
    s_smp.vbus   = (float)vb * VBUS_K;
    s_smp.vout   = (float)vo * VOUT_K;
    s_new = 1;
}
