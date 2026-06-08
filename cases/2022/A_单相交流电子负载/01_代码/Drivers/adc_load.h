#ifndef __ADC_LOAD_H__
#define __ADC_LOAD_H__
#include <stdint.h>

typedef struct {
    float v_grid; /* 电网电压 */
    float i_grid; /* 输入电流 */
    float vbus;   /* DC 母线 */
    float vout;   /* 回馈输出 */
} load_sample_t;

void adc_load_init(void);
void adc_load_start(void);
uint8_t adc_load_get(load_sample_t *out);
void adc_load_on_isr(uint16_t v_raw, uint16_t i_raw, uint16_t vb_raw, uint16_t vo_raw);
#endif
