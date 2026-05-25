/**
 * @file    adc_threephase.c
 */
#include "adc_threephase.h"
#include "../config.h"

static volatile pq_sample_t s_sample;
static volatile uint8_t s_new = 0;

void adc3p_init(void)  { s_new = 0; }
void adc3p_start(void) { }
void adc3p_stop(void)  { }
void adc3p_auto_zero(void) { }

uint8_t adc3p_get_sample(pq_sample_t *out)
{
    if ((!out) || (!s_new)) return 0;
    *out = s_sample;
    s_new = 0;
    return 1;
}

void adc3p_on_sample(const uint16_t *raw, uint8_t n)
{
    if ((!raw) || (n < 8)) return;
    pq_sample_t s;
    s.va   = ((float)raw[0] - 2048.0f) * ADC_VPHASE_K;
    s.vb   = ((float)raw[1] - 2048.0f) * ADC_VPHASE_K;
    s.vc   = ((float)raw[2] - 2048.0f) * ADC_VPHASE_K;
    s.ia   = ((float)raw[3] - 2048.0f) * ADC_IPHASE_K;
    s.ib   = ((float)raw[4] - 2048.0f) * ADC_IPHASE_K;
    s.ic   = ((float)raw[5] - 2048.0f) * ADC_IPHASE_K;
    s.vbus = (float)raw[6] * ADC_VBUS_K;
    s.vout = (float)raw[7] * ADC_VOUT_K;
    s.il   = (n > 8) ? (((float)raw[8] - 2048.0f) * ADC_IOUT_K) : 0.0f;
    s_sample = s;
    s_new = 1;
}
