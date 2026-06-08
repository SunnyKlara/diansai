/**
 * 频率响应测量与存储模块 —— 实现（纯算法层）
 *
 * Steering 强制约束（已通过审计）：
 *   ✅ 不引入 stm32xxx_hal.h / arm_math.h / Drivers 头
 *   ✅ 仅依赖 stdint.h / math.h / config.h / string.h
 *   ✅ 硬件动作通过 FreqResp_Callbacks_t 注入
 *
 * 流程：
 *   1. Coarse sweep 20 个对数分布点 → 识别滤波器类型
 *   2. 找截止频率附近 → fine sweep 30 个点
 *   3. 相位解缠绕 + 按频率排序 → 存到 freq_table
 *   4. 推理阶段调用 Interpolate() 二分查找 + 线性插值
 */

#include "freq_response.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

/* ========================================================================
 * 内部状态
 * ===================================================================== */

static FreqPoint_t  freq_table[FREQ_RESP_MAX_POINTS];
static uint16_t     freq_table_len = 0;
static FilterType_t identified_type = FILT_UNKNOWN;

static FreqResp_Callbacks_t s_cb = {0, 0, 0};

/* ========================================================================
 * 单频测量：纯算法（外部传入数据）
 * ===================================================================== */

void FreqResp_MeasureOnePoint(const int16_t *in, const int16_t *out,
                              uint16_t length, float sample_rate,
                              float freq_hz, float *gain, float *phase)
{
    if (!in || !out || !gain || !phase || length == 0 || sample_rate < 1.0f) {
        if (gain)  *gain  = 0.0f;
        if (phase) *phase = 0.0f;
        return;
    }

    /* 单频 DFT：在频率 freq_hz 处提取实虚分量 */
    float target_bin = freq_hz * (float)length / sample_rate;

    float in_sin = 0, in_cos = 0;
    float out_sin = 0, out_cos = 0;

    for (uint16_t n = 0; n < length; n++) {
        float angle = 2.0f * PI * target_bin * (float)n / (float)length;
        float s = sinf(angle);
        float c = cosf(angle);

        in_sin  += (float)in[n]  * s;
        in_cos  += (float)in[n]  * c;
        out_sin += (float)out[n] * s;
        out_cos += (float)out[n] * c;
    }

    float in_mag  = sqrtf(in_sin  * in_sin  + in_cos  * in_cos);
    float out_mag = sqrtf(out_sin * out_sin + out_cos * out_cos);

    *gain = (in_mag > 10.0f) ? (out_mag / in_mag) : 0.0f;

    float in_phase  = atan2f(in_sin,  in_cos);
    float out_phase = atan2f(out_sin, out_cos);
    float ph = out_phase - in_phase;
    while (ph >  PI) ph -= 2.0f * PI;
    while (ph < -PI) ph += 2.0f * PI;
    *phase = ph;
}

/* ========================================================================
 * 内部辅助
 * ===================================================================== */

static FilterType_t identify_filter(void)
{
    if (freq_table_len < 9) return FILT_UNKNOWN;

    uint16_t third = freq_table_len / 3;
    float g_low = 0, g_mid = 0, g_high = 0;

    for (uint16_t i = 0; i < third; i++)            g_low  += freq_table[i].gain;
    for (uint16_t i = third; i < 2 * third; i++)    g_mid  += freq_table[i].gain;
    for (uint16_t i = 2 * third; i < freq_table_len; i++) g_high += freq_table[i].gain;

    g_low  /= (float)third;
    g_mid  /= (float)third;
    g_high /= (float)(freq_table_len - 2 * third);

    if (g_low  < 0.001f) g_low  = 0.001f;
    if (g_mid  < 0.001f) g_mid  = 0.001f;
    if (g_high < 0.001f) g_high = 0.001f;

    float ratio_lh = g_low / g_high;
    float ratio_ml = g_mid / g_low;
    float ratio_mh = g_mid / g_high;

    if (ratio_lh > 3.0f)                            return FILT_LOWPASS;
    if (ratio_lh < 0.33f)                           return FILT_HIGHPASS;
    if (ratio_ml > 2.0f && ratio_mh > 2.0f)         return FILT_BANDPASS;
    if (ratio_ml < 0.5f && ratio_mh < 0.5f)         return FILT_BANDSTOP;
    return FILT_UNKNOWN;
}

static void phase_unwrap(void)
{
    for (uint16_t i = 1; i < freq_table_len; i++) {
        float diff = freq_table[i].phase_rad - freq_table[i-1].phase_rad;
        while (diff >  PI) { freq_table[i].phase_rad -= 2.0f * PI; diff -= 2.0f * PI; }
        while (diff < -PI) { freq_table[i].phase_rad += 2.0f * PI; diff += 2.0f * PI; }
    }
}

static void sort_by_freq(void)
{
    for (uint16_t i = 1; i < freq_table_len; i++) {
        FreqPoint_t key = freq_table[i];
        int16_t j = (int16_t)i - 1;
        while (j >= 0 && freq_table[j].frequency > key.frequency) {
            freq_table[j + 1] = freq_table[j];
            j--;
        }
        freq_table[j + 1] = key;
    }
}

/* ========================================================================
 * 测量一个频率点（包装：回调 + 算法）
 * ===================================================================== */

static uint8_t measure_with_hw(float freq_hz, float *gain, float *phase)
{
    if (!s_cb.dds_set_freq || !s_cb.delay_ms || !s_cb.capture) {
        *gain = 0.0f; *phase = 0.0f;
        return 0;
    }

    /* 1. DDS 设频 */
    s_cb.dds_set_freq(freq_hz);

    /* 2. 等待信号稳定 */
    float settle_ms = (float)SWEEP_SETTLE_CYCLES * 1000.0f / freq_hz;
    if (settle_ms < 5.0f)   settle_ms = 5.0f;
    if (settle_ms > 200.0f) settle_ms = 200.0f;
    s_cb.delay_ms((uint32_t)(settle_ms + 0.5f));

    /* 3. 同步采集 */
    const int16_t *in_data; const int16_t *out_data;
    float sample_rate; uint16_t length;
    if (!s_cb.capture(freq_hz, &in_data, &out_data, &sample_rate, &length)) {
        *gain = 0.0f; *phase = 0.0f;
        return 0;
    }

    /* 4. DFT */
    FreqResp_MeasureOnePoint(in_data, out_data, length, sample_rate,
                             freq_hz, gain, phase);
    return 1;
}

/* ========================================================================
 * 公开 API
 * ===================================================================== */

void FreqResp_BindCallbacks(const FreqResp_Callbacks_t *cb)
{
    if (cb) s_cb = *cb;
}

uint16_t FreqResp_Learn(void)
{
    freq_table_len = 0;

    /* 粗扫：20 点对数分布 */
    static const float coarse_freqs[] = {
        100, 150, 200, 300, 500, 700,
        1000, 1500, 2000, 3000, 5000, 7000,
        10000, 15000, 20000, 30000, 40000, 50000,
        70000, 100000
    };
    uint16_t num_coarse = sizeof(coarse_freqs) / sizeof(coarse_freqs[0]);

    for (uint16_t i = 0; i < num_coarse; i++) {
        float g, p;
        if (measure_with_hw(coarse_freqs[i], &g, &p)) {
            freq_table[freq_table_len].frequency = coarse_freqs[i];
            freq_table[freq_table_len].gain = g;
            freq_table[freq_table_len].phase_rad = p;
            freq_table_len++;
        }
    }

    sort_by_freq();
    phase_unwrap();
    identified_type = identify_filter();

    /* 细扫：截止频率附近 30 点 */
    float max_gain = 0;
    for (uint16_t i = 0; i < freq_table_len; i++) {
        if (freq_table[i].gain > max_gain) max_gain = freq_table[i].gain;
    }
    float cutoff_gain = max_gain * 0.707f;

    float fc_low = coarse_freqs[0], fc_high = coarse_freqs[num_coarse - 1];
    for (uint16_t i = 1; i < freq_table_len; i++) {
        if (freq_table[i-1].gain > cutoff_gain && freq_table[i].gain < cutoff_gain) {
            fc_low = freq_table[i-1].frequency;
            fc_high = freq_table[i].frequency;
            break;
        }
        if (freq_table[i-1].gain < cutoff_gain && freq_table[i].gain > cutoff_gain) {
            fc_low = freq_table[i-1].frequency;
            fc_high = freq_table[i].frequency;
        }
    }

    float fc_range = fc_high - fc_low;
    if (fc_range < 100) fc_range = 100;
    float fc_step = fc_range / 30.0f;

    for (float f = fc_low; f <= fc_high && freq_table_len < FREQ_RESP_MAX_POINTS; f += fc_step) {
        uint8_t duplicate = 0;
        for (uint16_t i = 0; i < freq_table_len; i++) {
            if (fabsf(freq_table[i].frequency - f) < fc_step * 0.3f) {
                duplicate = 1; break;
            }
        }
        if (duplicate) continue;

        float g, p;
        if (measure_with_hw(f, &g, &p)) {
            freq_table[freq_table_len].frequency = f;
            freq_table[freq_table_len].gain = g;
            freq_table[freq_table_len].phase_rad = p;
            freq_table_len++;
        }
    }

    sort_by_freq();
    phase_unwrap();

    return freq_table_len;
}

FilterType_t FreqResp_GetFilterType(void)
{
    return identified_type;
}

void FreqResp_Interpolate(float freq_hz, float *gain, float *phase)
{
    if (!gain || !phase) return;
    if (freq_table_len == 0) {
        *gain = 1.0f; *phase = 0.0f; return;
    }
    if (freq_hz <= freq_table[0].frequency) {
        *gain = freq_table[0].gain;
        *phase = freq_table[0].phase_rad; return;
    }
    if (freq_hz >= freq_table[freq_table_len - 1].frequency) {
        *gain = freq_table[freq_table_len - 1].gain;
        *phase = freq_table[freq_table_len - 1].phase_rad; return;
    }

    /* 二分查找 */
    uint16_t lo = 0, hi = freq_table_len - 1;
    while (hi - lo > 1) {
        uint16_t mid = (lo + hi) / 2;
        if (freq_table[mid].frequency <= freq_hz) lo = mid;
        else                                       hi = mid;
    }

    float f0 = freq_table[lo].frequency;
    float f1 = freq_table[hi].frequency;
    float t = (freq_hz - f0) / (f1 - f0);
    *gain  = freq_table[lo].gain      * (1.0f - t) + freq_table[hi].gain      * t;
    *phase = freq_table[lo].phase_rad * (1.0f - t) + freq_table[hi].phase_rad * t;
}

FreqPoint_t *FreqResp_GetTable(uint16_t *length)
{
    if (length) *length = freq_table_len;
    return freq_table;
}
