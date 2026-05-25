/**
 * @file    auto_tune.c
 */
#include "auto_tune.h"
#include "../config.h"

void auto_tune_init(void){}

uint8_t auto_tune_scan(uint32_t f_start, uint32_t f_end, uint32_t step,
                       channel_info_t *out, uint8_t max_n)
{
    if (!out) return 0;
    uint8_t n = 0;
    for (uint32_t f = f_start; f < f_end && n < max_n; f += step) {
        out[n].freq_hz = f;
        out[n].rssi_dbm = -90.0f;     /* 占位：实测 ADC RMS → dBm */
        out[n].snr_db = 0;
        n++;
    }
    return n;
}

uint32_t auto_tune_lock(uint32_t f_target)
{
    /* 真机：在 f_target ±1kHz 范围内扫，找 RSSI 最大点锁定 */
    return f_target;
}
