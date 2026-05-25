#include "param_meas.h"

static float min_f(float a, float b){ return a<b?a:b; }
static float max_f(float a, float b){ return a>b?a:b; }

void meas_run(const uint16_t *raw, uint32_t len,
              float fs_hz, float v_per_lsb, float offset,
              meas_result_t *out)
{
    if (!raw || !out || len < 16) return;

    float vmin =  1e9f, vmax = -1e9f;
    for (uint32_t i = 0; i < len; i++) {
        float v = ((float)raw[i] - offset) * v_per_lsb;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    out->vpp = vmax - vmin;
    float vmid = 0.5f * (vmax + vmin);

    /* 过零 + 线性插值（阈值 = vmid）*/
    int32_t first_idx = -1, last_idx = -1;
    int32_t count = 0;
    float prev = ((float)raw[0] - offset) * v_per_lsb - vmid;
    for (uint32_t i = 1; i < len; i++) {
        float cur = ((float)raw[i] - offset) * v_per_lsb - vmid;
        if (prev < 0 && cur >= 0) {
            float frac = -prev / (cur - prev);
            float idx_f = (float)(i - 1) + frac;
            if (first_idx < 0) first_idx = (int32_t)(idx_f * 1000.0f);
            last_idx = (int32_t)(idx_f * 1000.0f);
            count++;
        }
        prev = cur;
    }
    if (count >= 2) {
        float diff_idx = (float)(last_idx - first_idx) / 1000.0f;
        float period_pts = diff_idx / (float)(count - 1);
        out->freq_hz = fs_hz / period_pts;
    } else {
        out->freq_hz = 0.0f;
    }

    /* 占空比（高于 vmid 的占比） */
    uint32_t hi = 0;
    for (uint32_t i = 0; i < len; i++) {
        float v = ((float)raw[i] - offset) * v_per_lsb;
        if (v > vmid) hi++;
    }
    out->duty_pct = (float)hi / (float)len * 100.0f;
}
