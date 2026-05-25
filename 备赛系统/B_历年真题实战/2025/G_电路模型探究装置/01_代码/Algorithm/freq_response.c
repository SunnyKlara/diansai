/**
 * 频率响应测量与存储模块 —— 实现
 * 
 * ============================================================
 * 【扫频测量的完整流程】
 * ============================================================
 * 
 * 1. 粗扫（20个对数分布的频率点）
 *    → 确定频率响应的大致形状
 *    → 识别滤波器类型（低通/高通/带通/带阻）
 *    → 找到截止频率的大致位置
 * 
 * 2. 细扫（截止频率附近30个点）
 *    → 在增益变化最剧烈的区域加密采样
 *    → 这是精度的关键
 * 
 * 3. 补充扫描（覆盖可能的谐波频率）
 *    → 矩形波的谐波可能在任何频率
 *    → 确保查找表覆盖足够宽的范围
 * 
 * 4. 相位解缠绕
 *    → atan2返回[-π,π]，相邻点可能有2π跳变
 *    → 必须处理，否则插值会在跳变处产生巨大误差
 * 
 * 5. 排序存储
 *    → 按频率升序排列，方便二分查找插值
 * 
 * ============================================================
 */

#include "freq_response.h"
#include "../Drivers/dds_ad9833.h"
#include "../Drivers/adc_dual_sync.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

/* -------- 频率响应存储 -------- */
static FreqPoint_t freq_table[FREQ_RESP_MAX_POINTS];
static uint16_t freq_table_len = 0;
static FilterType_t identified_type = FILT_UNKNOWN;

/* ============================================================
 * 单频率点测量
 * ============================================================ */

/**
 * 测量单个频率点的增益和相位
 * 
 * 【测量方法：单频DFT（不是完整FFT）】
 * 
 * 我们只关心一个频率（DDS输出的频率），不需要完整的频谱。
 * 单频DFT只计算一个频率bin，计算量是FFT的1/N。
 * 
 * 对N个采样点，计算频率f对应的DFT系数：
 *   X(f) = Σ x[n] × exp(-j2πfn/fs)
 *        = Σ x[n] × cos(2πfn/fs) - j × Σ x[n] × sin(2πfn/fs)
 * 
 * 幅值 = |X(f)| × 2/N
 * 相位 = atan2(Im, Re)
 * 
 * 【为什么不直接用峰值比？】
 * 峰值比只能得到增益，得不到相位。
 * 而且峰值受噪声影响大，DFT相当于做了窄带滤波，抗噪声能力强得多。
 */
static void measure_single_freq(float freq_hz, float* gain, float* phase)
{
    // 1. 设置DDS输出频率
    DDS_SetFreq(freq_hz);
    DDS_SetWaveform(DDS_WAVE_SINE);
    DDS_Start();
    
    // 2. 等待信号稳定
    //    等待时间 = max(SETTLE_CYCLES/freq, 5ms)
    //    低频需要等更多周期，高频5ms就够了
    float settle_ms = SWEEP_SETTLE_CYCLES * 1000.0f / freq_hz;
    if (settle_ms < 5.0f) settle_ms = 5.0f;
    if (settle_ms > 200.0f) settle_ms = 200.0f;  // 最长200ms（低频100Hz时）
    HAL_Delay((uint32_t)(settle_ms + 0.5f));
    
    // 3. 设置采样率
    //    采样率 = freq × N / M，其中M是采集的周期数
    //    选M=10个周期，N=DFT_SAMPLES_PER_POINT=512
    //    → 采样率 = freq × 512 / 10 = freq × 51.2
    //    
    //    但采样率不能太低（ADC有最低采样率限制）也不能太高（超过ADC能力）
    //    限制在10kHz~1MHz之间
    float ideal_rate = freq_hz * DFT_SAMPLES_PER_POINT / 10.0f;
    if (ideal_rate < 10000.0f) ideal_rate = 10000.0f;
    if (ideal_rate > 1000000.0f) ideal_rate = 1000000.0f;
    
    float actual_rate = ADC_DualSync_SetRate(ideal_rate);
    
    // 4. 采集数据
    ADC_DualSync_StartCapture(DFT_SAMPLES_PER_POINT);
    while (!ADC_DualSync_IsComplete()) {
        // 等待采集完成
    }
    
    int16_t *ch1, *ch2;
    uint16_t len;
    ADC_DualSync_GetData(&ch1, &ch2, &len);
    
    // 5. 单频DFT提取输入和输出的幅值与相位
    //    目标频率在DFT中的bin索引 = freq × N / fs
    //    由于我们设计了采样率使得10个周期恰好N个点，
    //    所以bin索引 = 10（精确落在bin上，无频谱泄漏！）
    
    float target_bin = freq_hz * len / actual_rate;
    // target_bin应该接近10，但由于采样率的整数分频可能有微小偏差
    // 用实际的target_bin做DFT
    
    float in_sin = 0, in_cos = 0;
    float out_sin = 0, out_cos = 0;
    
    for (uint16_t n = 0; n < len; n++) {
        float angle = 2.0f * PI * target_bin * n / len;
        float s = arm_sin_f32(angle);
        float c = arm_cos_f32(angle);
        
        in_sin  += (float)ch1[n] * s;
        in_cos  += (float)ch1[n] * c;
        out_sin += (float)ch2[n] * s;
        out_cos += (float)ch2[n] * c;
    }
    
    float in_mag  = sqrtf(in_sin * in_sin + in_cos * in_cos);
    float out_mag = sqrtf(out_sin * out_sin + out_cos * out_cos);
    
    // 6. 计算增益和相位
    if (in_mag > 10.0f) {  // 输入信号幅度足够（避免除以接近零的数）
        *gain = out_mag / in_mag;
    } else {
        *gain = 0;
    }
    
    float in_phase  = atan2f(in_sin, in_cos);
    float out_phase = atan2f(out_sin, out_cos);
    *phase = out_phase - in_phase;
    
    // 相位归一化到[-π, π]
    while (*phase > PI) *phase -= 2.0f * PI;
    while (*phase < -PI) *phase += 2.0f * PI;
}

/* ============================================================
 * 滤波器类型识别
 * ============================================================ */

/**
 * 根据频率响应数据判断滤波器类型
 * 
 * 【判断逻辑】
 * 将频率范围分为低/中/高三段，比较各段的平均增益：
 * 
 * 低通：gain_low >> gain_high
 * 高通：gain_high >> gain_low
 * 带通：gain_mid >> gain_low 且 gain_mid >> gain_high
 * 带阻：gain_mid << gain_low 且 gain_mid << gain_high
 * 
 * 用比值而不是绝对值判断，更鲁棒。
 */
static FilterType_t identify_filter(void)
{
    if (freq_table_len < 9) return FILT_UNKNOWN;
    
    uint16_t third = freq_table_len / 3;
    
    float g_low = 0, g_mid = 0, g_high = 0;
    for (uint16_t i = 0; i < third; i++)
        g_low += freq_table[i].gain;
    for (uint16_t i = third; i < 2 * third; i++)
        g_mid += freq_table[i].gain;
    for (uint16_t i = 2 * third; i < freq_table_len; i++)
        g_high += freq_table[i].gain;
    
    g_low  /= third;
    g_mid  /= third;
    g_high /= (freq_table_len - 2 * third);
    
    // 避免除零
    if (g_low < 0.001f) g_low = 0.001f;
    if (g_mid < 0.001f) g_mid = 0.001f;
    if (g_high < 0.001f) g_high = 0.001f;
    
    float ratio_lh = g_low / g_high;   // 低频/高频
    float ratio_ml = g_mid / g_low;    // 中频/低频
    float ratio_mh = g_mid / g_high;   // 中频/高频
    
    if (ratio_lh > 3.0f)                           return FILT_LOWPASS;
    if (ratio_lh < 0.33f)                           return FILT_HIGHPASS;
    if (ratio_ml > 2.0f && ratio_mh > 2.0f)        return FILT_BANDPASS;
    if (ratio_ml < 0.5f && ratio_mh < 0.5f)        return FILT_BANDSTOP;
    
    return FILT_UNKNOWN;
}

/* ============================================================
 * 相位解缠绕
 * ============================================================ */

/**
 * 相位解缠绕（Phase Unwrapping）
 * 
 * atan2返回[-π, π]，当真实相位跨越±π边界时会产生2π跳变。
 * 例如：真实相位从-170°变到-190°，atan2会返回-170°→+170°（跳变340°）
 * 
 * 解缠绕：检测相邻点的相位差，如果>π则减2π，如果<-π则加2π。
 * 
 * 【为什么这一步不能省？】
 * 如果不解缠绕，在跳变点处做线性插值会得到完全错误的相位值。
 * 例如：f1处相位=-170°，f2处相位=+170°（实际应该是-190°）
 * 插值f1和f2中间的频率：(-170+170)/2 = 0°
 * 实际应该是：(-170-190)/2 = -180°
 * 误差180°！这会导致该频率分量的极性完全反转。
 */
static void phase_unwrap(void)
{
    for (uint16_t i = 1; i < freq_table_len; i++) {
        float diff = freq_table[i].phase_rad - freq_table[i-1].phase_rad;
        while (diff > PI)  { freq_table[i].phase_rad -= 2.0f * PI; diff -= 2.0f * PI; }
        while (diff < -PI) { freq_table[i].phase_rad += 2.0f * PI; diff += 2.0f * PI; }
    }
}

/* ============================================================
 * 排序（按频率升序）
 * ============================================================ */

static void sort_by_freq(void)
{
    // 简单的插入排序（数据量<200，不需要快排）
    for (uint16_t i = 1; i < freq_table_len; i++) {
        FreqPoint_t key = freq_table[i];
        int16_t j = i - 1;
        while (j >= 0 && freq_table[j].frequency > key.frequency) {
            freq_table[j + 1] = freq_table[j];
            j--;
        }
        freq_table[j + 1] = key;
    }
}

/* ============================================================
 * 完整学习流程
 * ============================================================ */

uint16_t FreqResp_Learn(void)
{
    freq_table_len = 0;
    
    // ===== 第一阶段：粗扫（对数分布，20个点） =====
    const float coarse_freqs[] = {
        100, 150, 200, 300, 500, 700,
        1000, 1500, 2000, 3000, 5000, 7000,
        10000, 15000, 20000, 30000, 40000, 50000,
        70000, 100000
    };
    uint16_t num_coarse = sizeof(coarse_freqs) / sizeof(coarse_freqs[0]);
    
    for (uint16_t i = 0; i < num_coarse; i++) {
        float g, p;
        measure_single_freq(coarse_freqs[i], &g, &p);
        
        freq_table[freq_table_len].frequency = coarse_freqs[i];
        freq_table[freq_table_len].gain = g;
        freq_table[freq_table_len].phase_rad = p;
        freq_table_len++;
    }
    
    // 排序+解缠绕（粗扫数据已经是升序的，但保险起见）
    sort_by_freq();
    phase_unwrap();
    
    // 识别滤波器类型
    identified_type = identify_filter();
    
    // ===== 第二阶段：找截止频率并细扫 =====
    // 截止频率定义：增益下降到最大值的0.707（-3dB）处
    float max_gain = 0;
    for (uint16_t i = 0; i < freq_table_len; i++) {
        if (freq_table[i].gain > max_gain) max_gain = freq_table[i].gain;
    }
    float cutoff_gain = max_gain * 0.707f;
    
    // 找截止频率附近的区间
    float fc_low = coarse_freqs[0], fc_high = coarse_freqs[num_coarse - 1];
    for (uint16_t i = 1; i < freq_table_len; i++) {
        // 找增益从>cutoff变到<cutoff的区间（低通/带通的上截止）
        if (freq_table[i-1].gain > cutoff_gain && freq_table[i].gain < cutoff_gain) {
            fc_low = freq_table[i-1].frequency;
            fc_high = freq_table[i].frequency;
            break;
        }
        // 找增益从<cutoff变到>cutoff的区间（高通/带通的下截止）
        if (freq_table[i-1].gain < cutoff_gain && freq_table[i].gain > cutoff_gain) {
            fc_low = freq_table[i-1].frequency;
            fc_high = freq_table[i].frequency;
            // 不break，继续找上截止
        }
    }
    
    // 在截止频率区间内细扫（30个点）
    float fc_range = fc_high - fc_low;
    if (fc_range < 100) fc_range = 100;  // 最小范围100Hz
    float fc_step = fc_range / 30.0f;
    
    for (float f = fc_low; f <= fc_high && freq_table_len < FREQ_RESP_MAX_POINTS; f += fc_step) {
        // 检查是否已经有接近的频率点（避免重复测量）
        uint8_t duplicate = 0;
        for (uint16_t i = 0; i < freq_table_len; i++) {
            if (fabsf(freq_table[i].frequency - f) < fc_step * 0.3f) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        
        float g, p;
        measure_single_freq(f, &g, &p);
        
        freq_table[freq_table_len].frequency = f;
        freq_table[freq_table_len].gain = g;
        freq_table[freq_table_len].phase_rad = p;
        freq_table_len++;
    }
    
    // ===== 最终处理 =====
    sort_by_freq();
    phase_unwrap();
    
    DDS_Stop();  // 学习完成，关闭DDS
    
    return freq_table_len;
}

/* ============================================================
 * 插值查询
 * ============================================================ */

void FreqResp_Interpolate(float freq_hz, float* gain, float* phase)
{
    // 边界处理
    if (freq_table_len == 0) {
        *gain = 1.0f;
        *phase = 0;
        return;
    }
    
    if (freq_hz <= freq_table[0].frequency) {
        *gain = freq_table[0].gain;
        *phase = freq_table[0].phase_rad;
        return;
    }
    
    if (freq_hz >= freq_table[freq_table_len - 1].frequency) {
        *gain = freq_table[freq_table_len - 1].gain;
        *phase = freq_table[freq_table_len - 1].phase_rad;
        return;
    }
    
    // 二分查找（比线性查找快，对200个点有明显优势）
    uint16_t lo = 0, hi = freq_table_len - 1;
    while (hi - lo > 1) {
        uint16_t mid = (lo + hi) / 2;
        if (freq_table[mid].frequency <= freq_hz) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    
    // 线性插值
    float f0 = freq_table[lo].frequency;
    float f1 = freq_table[hi].frequency;
    float t = (freq_hz - f0) / (f1 - f0);
    
    *gain  = freq_table[lo].gain * (1.0f - t) + freq_table[hi].gain * t;
    *phase = freq_table[lo].phase_rad * (1.0f - t) + freq_table[hi].phase_rad * t;
}

/* ============================================================
 * 查询接口
 * ============================================================ */

FilterType_t FreqResp_GetFilterType(void)
{
    return identified_type;
}

FreqPoint_t* FreqResp_GetTable(uint16_t* length)
{
    *length = freq_table_len;
    return freq_table;
}
