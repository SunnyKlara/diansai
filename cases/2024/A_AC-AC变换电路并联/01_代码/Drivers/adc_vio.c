/**
 * @file    adc_vio.c
 */
#include "adc_vio.h"
#include "../config.h"
#include <math.h>

static volatile uint16_t s_vin_raw = 0, s_vout_raw = 0, s_iout_raw = 2048;
static volatile uint8_t  s_new = 0;
static float s_iout_offset = IOUT_OFFSET_LSB;

#define RMS_BUF_LEN 200U
static float s_vout_sq_sum = 0, s_iout_sq_sum = 0, s_vin_sq_sum = 0;
static uint32_t s_rms_count = 0;
static ac_rms_t s_rms = {0,0,0};

void adc_vio_init(void)
{
    s_vin_raw = 0; s_vout_raw = 0; s_iout_raw = 2048;
    s_iout_offset = IOUT_OFFSET_LSB;
    s_new = 0;
}

void adc_vio_start(void) { /* HAL DMA + TIM trigger */ }
void adc_vio_stop(void)  { }

void adc_vio_auto_zero(void)
{
    /* 真机：N 点平均，写入 s_iout_offset */
}

uint8_t adc_vio_get_sample(ac_sample_t *out)
{
    if ((out == 0) || (s_new == 0)) return 0;
    out->vin  = (float)s_vin_raw  * VIN_SCALE_V_PER_LSB;
    out->vout = (float)s_vout_raw * VOUT_SCALE_V_PER_LSB;
    out->iout = ((float)s_iout_raw - s_iout_offset) * IOUT_SCALE_A_PER_LSB;
    s_new = 0;
    return 1;
}

void adc_vio_get_rms(ac_rms_t *out)
{
    if (out == 0) return;
    *out = s_rms;
}

void adc_vio_on_sample(uint16_t vin_raw, uint16_t vout_raw, uint16_t iout_raw)
{
    s_vin_raw = vin_raw; s_vout_raw = vout_raw; s_iout_raw = iout_raw;
    s_new = 1;

    float vin  = (float)vin_raw  * VIN_SCALE_V_PER_LSB;
    float vout = (float)vout_raw * VOUT_SCALE_V_PER_LSB;
    float iout = ((float)iout_raw - s_iout_offset) * IOUT_SCALE_A_PER_LSB;

    s_vin_sq_sum  += vin  * vin;
    s_vout_sq_sum += vout * vout;
    s_iout_sq_sum += iout * iout;
    s_rms_count++;

    if (s_rms_count >= RMS_BUF_LEN) {
        s_rms.vin_rms  = sqrtf(s_vin_sq_sum  / (float)s_rms_count);
        s_rms.vout_rms = sqrtf(s_vout_sq_sum / (float)s_rms_count);
        s_rms.iout_rms = sqrtf(s_iout_sq_sum / (float)s_rms_count);
        s_vin_sq_sum = s_vout_sq_sum = s_iout_sq_sum = 0;
        s_rms_count = 0;
    }
}
