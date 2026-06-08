/**
 * @file    tdr_analyze.c
 */
#include "tdr_analyze.h"
#include "../config.h"

void tdr_analyze(const float *wave, uint32_t len, tdr_result_t *out)
{
    if (!wave || !out) return;

    /* 找入射峰：最大正峰 */
    uint32_t i_inc = 0;
    float v_inc = wave[0];
    for (uint32_t i = 1; i < len / 4; i++) {
        if (wave[i] > v_inc) { v_inc = wave[i]; i_inc = i; }
    }

    /* 找反射峰：i_inc + 100 起的最大幅度（正或负）*/
    uint32_t start = i_inc + 30;
    if (start >= len) { out->length_m = 0; return; }
    uint32_t i_ref = start;
    float v_ref = 0.0f;
    for (uint32_t i = start; i < len; i++) {
        float a = wave[i] > 0 ? wave[i] : -wave[i];
        float b = v_ref > 0 ? v_ref : -v_ref;
        if (a > b) { v_ref = wave[i]; i_ref = i; }
    }

    /* 双程时间 */
    float dt_per_sample = 1.0f / TDR_EQUIV_FS_HZ;
    float t_2way_s = (float)(i_ref - i_inc) * dt_per_sample;
    out->length_m = t_2way_s * VP_MPS / 2.0f;

    /* 反射系数 */
    if (v_inc > 1e-3f) {
        out->reflection = v_ref / v_inc;
        out->load_ohm = Z0_OHM * (1.0f + out->reflection) /
                        (1.0f - out->reflection + 1e-6f);
        if (out->reflection >  0.95f) out->state = TDR_OPEN;
        else if (out->reflection < -0.95f) out->state = TDR_SHORT;
        else if ((out->reflection > -0.05f) && (out->reflection < 0.05f))
            out->state = TDR_MATCH;
        else out->state = TDR_OTHER;
    } else {
        out->reflection = 0; out->load_ohm = 0; out->state = TDR_NONE;
    }
}
