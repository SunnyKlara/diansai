#ifndef __TDR_ANALYZE_H__
#define __TDR_ANALYZE_H__
#include <stdint.h>

typedef enum {
    TDR_NONE = 0,
    TDR_OPEN,
    TDR_SHORT,
    TDR_MATCH,
    TDR_OTHER
} tdr_state_t;

typedef struct {
    float length_m;
    float reflection;     /* ρ */
    float load_ohm;       /* 终端阻抗（Ω）*/
    tdr_state_t state;
} tdr_result_t;

void tdr_analyze(const float *wave, uint32_t len, tdr_result_t *out);
#endif
