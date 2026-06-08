/**
 * @file    adc_vio.h
 * @brief   双 ADC 同步：Vin / Vout / Iout
 */
#ifndef __ADC_VIO_H__
#define __ADC_VIO_H__

#include <stdint.h>

typedef struct {
    float vin;          /* V */
    float vout;         /* V */
    float iout;         /* A */
} ac_sample_t;

typedef struct {
    float vin_rms;
    float vout_rms;
    float iout_rms;
} ac_rms_t;

void adc_vio_init(void);
void adc_vio_start(void);
void adc_vio_stop(void);
void adc_vio_auto_zero(void);
uint8_t adc_vio_get_sample(ac_sample_t *out);
void adc_vio_get_rms(ac_rms_t *out);
void adc_vio_on_sample(uint16_t vin_raw, uint16_t vout_raw, uint16_t iout_raw);

#endif
