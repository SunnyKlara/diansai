/**
 * @file    adc_threephase.h
 * @brief   STM32G474 三相同步采样：Va/Vb/Vc + Ia/Ib/Ic + Vbus + Vout/IL
 */
#ifndef __ADC_THREEPHASE_H__
#define __ADC_THREEPHASE_H__

#include <stdint.h>

typedef struct {
    float va, vb, vc;     /* 相电压（V）              */
    float ia, ib, ic;     /* 相电流（A）              */
    float vbus;           /* 母线电压（V）           */
    float vout;           /* Buck 输出电压（V）       */
    float il;             /* Buck 电感电流（A）      */
} pq_sample_t;

void adc3p_init(void);
void adc3p_start(void);
void adc3p_stop(void);
void adc3p_auto_zero(void);
uint8_t adc3p_get_sample(pq_sample_t *out);
void adc3p_on_sample(const uint16_t *raw_array, uint8_t n);

#endif
