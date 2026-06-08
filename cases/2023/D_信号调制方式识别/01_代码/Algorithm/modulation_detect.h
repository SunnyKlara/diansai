#ifndef __MODULATION_DETECT_H__
#define __MODULATION_DETECT_H__
#include <stdint.h>

typedef enum {
    MOD_AM    = 1,
    MOD_FM    = 2,
    MOD_2ASK  = 3,
    MOD_2FSK  = 4,
    MOD_2PSK  = 5
} mod_type_t;

typedef struct {
    mod_type_t type;
    float sigma_a_norm;
    float sigma_f_norm;
    float sigma_phi;
    float confidence;
} mod_result_t;

void mod_init(void);
void mod_analyze(const float *iq_i, const float *iq_q,
                 uint32_t len, mod_result_t *out);
#endif
