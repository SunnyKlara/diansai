#ifndef __ADC_DUAL_H__
#define __ADC_DUAL_H__
#include <stdint.h>
#define IMPEDANCE_FRAME_LEN 1024U
void adc_dual_init(void);
void adc_dual_start(void);
uint8_t adc_dual_get(uint16_t *a, uint16_t *b, uint32_t len);
void adc_dual_on_dma(const uint16_t *a, const uint16_t *b, uint32_t n);
#endif
