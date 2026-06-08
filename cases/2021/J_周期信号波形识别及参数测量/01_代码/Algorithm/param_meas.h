/**
 * @file    param_meas.h
 * @brief   频率（过零+插值）/ Vpp（多周期平均）/ 占空比（中值阈值）
 */
#ifndef __PARAM_MEAS_H__
#define __PARAM_MEAS_H__
#include <stdint.h>

typedef struct {
    float freq_hz;
    float vpp;
    float duty_pct;     /* 仅矩形波有效 */
} meas_result_t;

void meas_run(const uint16_t *raw, uint32_t len,
              float fs_hz, float v_per_lsb, float offset,
              meas_result_t *out);
#endif
