#ifndef __ADC_CAPTURE_H__
#define __ADC_CAPTURE_H__
#include <stdint.h>
#include "../config.h"
void adc_init(void);
void adc_set_fs(uint32_t fs_hz);
void adc_start(void);
void adc_stop(void);
uint8_t adc_get_frame(uint16_t *buf, uint32_t len);
void adc_on_dma(const uint16_t *raw, uint32_t len);
#endif
