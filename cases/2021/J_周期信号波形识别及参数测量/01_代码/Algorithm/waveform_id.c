/**
 * @file    waveform_id.c
 * @brief   波形识别 = 1024 点 FFT + 谐波比阈值
 */
#include "waveform_id.h"
#include "../config.h"
#include <math.h>

static float s_buf[FFT_SIZE * 2];
static float s_mag[FFT_SIZE];
static float s_hann[FFT_SIZE];

static void naive_fft(float *d, uint32_t N)
{
    /* 复用 2025B / 2024B 同款手写 Cooley-Tukey */
    uint32_t j = 0;
    for (uint32_t i = 0; i < N - 1; i++) {
        if (i < j) {
            float tr = d[2*i], ti = d[2*i+1];
            d[2*i]   = d[2*j];   d[2*i+1] = d[2*j+1];
            d[2*j]   = tr;       d[2*j+1] = ti;
        }
        uint32_t k = N >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
    for (uint32_t s = 1; (1u << s) <= N; s++) {
        uint32_t m = 1u << s, mh = m >> 1;
        float th = -TWO_PI / (float)m;
        float wr = cosf(th), wi = sinf(th);
        for (uint32_t k = 0; k < N; k += m) {
            float er = 1.0f, ei = 0.0f;
            for (uint32_t n = 0; n < mh; n++) {
                uint32_t a = (k + n) * 2;
                uint32_t b = (k + n + mh) * 2;
                float tr = er * d[b]   - ei * d[b+1];
                float ti = er * d[b+1] + ei * d[b];
                d[b]   = d[a]   - tr;
                d[b+1] = d[a+1] - ti;
                d[a]   += tr;   d[a+1] += ti;
                float nr = er * wr - ei * wi;
                float ni = er * wi + ei * wr;
                er = nr; ei = ni;
            }
        }
    }
}

void wf_id_init(void)
{
    for (uint32_t i = 0; i < FFT_SIZE; i++)
        s_hann[i] = 0.5f * (1.0f - cosf(TWO_PI * (float)i / (float)(FFT_SIZE - 1)));
}

void wf_id_analyze(const uint16_t *raw, uint32_t len,
                   float v_per_lsb, float offset, wf_result_t *out)
{
    if (!raw || !out) return;
    uint32_t N = (len > FFT_SIZE) ? FFT_SIZE : len;

    for (uint32_t i = 0; i < N; i++) {
        float v = ((float)raw[i] - offset) * v_per_lsb;
        s_buf[2*i]   = v * s_hann[i];
        s_buf[2*i+1] = 0.0f;
    }
    for (uint32_t i = N; i < FFT_SIZE; i++) { s_buf[2*i]=0; s_buf[2*i+1]=0; }

    naive_fft(s_buf, FFT_SIZE);
    for (uint32_t i = 0; i < FFT_SIZE; i++) {
        float r = s_buf[2*i], im = s_buf[2*i+1];
        s_mag[i] = sqrtf(r*r + im*im);
    }

    /* 找最大 bin = 基波 */
    uint32_t bin1 = 1;
    float maxv = s_mag[1];
    for (uint32_t i = 2; i < FFT_SIZE / 2; i++) {
        if (s_mag[i] > maxv) { maxv = s_mag[i]; bin1 = i; }
    }
    /* H1 = 该 bin（取 ±1 邻居最大）*/
    float h1 = s_mag[bin1];
    if (bin1 > 0          && s_mag[bin1-1] > h1) h1 = s_mag[bin1-1];
    if (bin1 + 1 < FFT_SIZE/2 && s_mag[bin1+1] > h1) h1 = s_mag[bin1+1];

    uint32_t b3 = bin1 * 3, b5 = bin1 * 5;
    float h3 = (b3 < FFT_SIZE/2) ? s_mag[b3] : 0.0f;
    float h5 = (b5 < FFT_SIZE/2) ? s_mag[b5] : 0.0f;

    if (h1 < 1e-3f) { out->type = WF_UNKNOWN; return; }
    out->h1_rms = h1; out->h3_rms = h3; out->h5_rms = h5;
    out->r3 = h3 / h1; out->r5 = h5 / h1;

    if (out->r3 < 0.05f)        out->type = WF_SINE;
    else if (out->r3 < 0.20f)   out->type = WF_TRI;
    else                        out->type = WF_SQUARE;
}
