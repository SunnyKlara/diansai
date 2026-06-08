/**
 * @file    impedance.c
 */
#include "impedance.h"
#include "../config.h"
#include <math.h>

void z_init(void) {}

static void single_freq_dft(const uint16_t *raw, uint32_t N,
                            float fs, float f0,
                            float *re_out, float *im_out)
{
    float re = 0, im = 0;
    float w = TWO_PI * f0 / fs;
    for (uint32_t i = 0; i < N; i++) {
        float v = ((float)raw[i] - ADC_OFFSET_LSB) * V_PER_LSB;
        re += v * cosf(w * (float)i);
        im -= v * sinf(w * (float)i);
    }
    *re_out = 2.0f * re / (float)N;
    *im_out = 2.0f * im / (float)N;
}

void z_measure(const uint16_t *ch_ref, const uint16_t *ch_dut,
               uint32_t len, float fs_hz, float r_ref,
               z_result_t *out)
{
    if (!ch_ref || !ch_dut || !out) return;

    float a_re, a_im;  /* 参考电阻电压 */
    float b_re, b_im;  /* 待测器件电压 */
    single_freq_dft(ch_ref, len, fs_hz, (float)DDS_FREQ_HZ, &a_re, &a_im);
    single_freq_dft(ch_dut, len, fs_hz, (float)DDS_FREQ_HZ, &b_re, &b_im);

    /* 电流相量 = V_ref / R_ref */
    float ir_re = a_re / r_ref, ir_im = a_im / r_ref;

    /* Z = V_dut / I_ref，复数除法 */
    float denom = ir_re * ir_re + ir_im * ir_im;
    if (denom < 1e-12f) { out->magnitude = 0; return; }
    float zr = (b_re * ir_re + b_im * ir_im) / denom;
    float zi = (b_im * ir_re - b_re * ir_im) / denom;

    out->resistance = zr;
    out->reactance  = zi;
    out->magnitude  = sqrtf(zr * zr + zi * zi);
    out->phase_deg  = atan2f(zi, zr) * 180.0f / PI;

    if (zi > 0.0f) {
        /* 感性：Z = jωL */
        out->L_uH = zi / (TWO_PI * (float)DDS_FREQ_HZ) * 1.0e6f;
        out->C_nF = 0.0f;
    } else {
        /* 容性：Z = -j/(ωC) */
        out->C_nF = 1.0f / (TWO_PI * (float)DDS_FREQ_HZ * (-zi)) * 1.0e9f;
        out->L_uH = 0.0f;
    }
    out->Q = (zr > 1e-3f) ? (float)fabs(zi) / zr : 0.0f;
}
