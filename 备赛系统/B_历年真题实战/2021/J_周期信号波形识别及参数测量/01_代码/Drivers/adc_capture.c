#include "adc_capture.h"
#include <string.h>

static uint16_t s_buf[FFT_SIZE];
static volatile uint8_t s_ready = 0;
static uint32_t s_fs = ADC_FS_MID_HZ;

void adc_init(void){ s_ready = 0; }
void adc_set_fs(uint32_t fs){ s_fs = fs; (void)s_fs; }
void adc_start(void){}
void adc_stop(void){}

uint8_t adc_get_frame(uint16_t *buf, uint32_t len)
{
    if (!buf || !s_ready || len < FFT_SIZE) return 0;
    memcpy(buf, s_buf, FFT_SIZE * sizeof(uint16_t));
    s_ready = 0;
    return 1;
}

void adc_on_dma(const uint16_t *raw, uint32_t len)
{
    if (!raw) return;
    uint32_t n = (len > FFT_SIZE) ? FFT_SIZE : len;
    memcpy(s_buf, raw, n * sizeof(uint16_t));
    s_ready = 1;
}
