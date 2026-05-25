#ifndef __AUTO_TUNE_H__
#define __AUTO_TUNE_H__
#include <stdint.h>

typedef struct {
    uint32_t freq_hz;
    float    rssi_dbm;
    float    snr_db;
} channel_info_t;

void auto_tune_init(void);
uint8_t auto_tune_scan(uint32_t f_start, uint32_t f_end, uint32_t step,
                        channel_info_t *out, uint8_t max_n);
uint32_t auto_tune_lock(uint32_t f_target);
#endif
