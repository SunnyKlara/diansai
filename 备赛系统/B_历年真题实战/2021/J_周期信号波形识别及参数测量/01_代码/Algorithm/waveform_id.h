/**
 * @file    waveform_id.h
 * @brief   波形识别（看 H3/H1, H5/H1 谐波比）
 */
#ifndef __WAVEFORM_ID_H__
#define __WAVEFORM_ID_H__
#include <stdint.h>
typedef enum {
    WF_UNKNOWN = 0,
    WF_SINE    = 1,
    WF_TRI     = 2,
    WF_SQUARE  = 3
} waveform_t;

typedef struct {
    waveform_t type;
    float h1_rms, h3_rms, h5_rms;
    float r3, r5;     /* H3/H1, H5/H1 */
} wf_result_t;

void wf_id_init(void);
void wf_id_analyze(const uint16_t *adc_data, uint32_t len,
                   float v_per_lsb, float offset_lsb,
                   wf_result_t *out);
#endif
