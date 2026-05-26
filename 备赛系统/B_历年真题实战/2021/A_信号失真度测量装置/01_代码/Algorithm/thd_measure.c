/**
 * 2021A THD测量核心算法
 * 
 * ============================================================
 * 【这道题最容易踩的坑：频谱泄漏】
 * ============================================================
 * 
 * 场景：信号基频1kHz，采样率100kHz，FFT点数1024
 * 频率分辨率 = 100000/1024 = 97.66Hz
 * 1kHz对应的bin = 1000/97.66 = 10.24 → 不是整数！
 * 
 * 这意味着1kHz的能量会"泄漏"到bin 10和bin 11，
 * 导致幅值测量偏低，THD计算不准。
 * 
 * 【三种解决方案，精度递增】
 * 
 * 方案1：加窗函数（简单，精度中等）
 *   汉宁窗可以将泄漏降低到-31dB
 *   平顶窗可以将幅度误差降低到<0.01dB
 *   但窗函数会降低频率分辨率
 *   
 * 方案2：同步采样（推荐，精度最高）
 *   调整采样率，使得N个采样点恰好覆盖M个信号周期
 *   这样基频恰好落在某个bin上，没有泄漏
 *   需要先粗测频率，再调整采样率
 *   
 * 方案3：Zoom FFT / CZT（复杂，精度高）
 *   对感兴趣的频率范围做高分辨率FFT
 *   计算量大，但不需要调整采样率
 * 
 * 【我的选择】方案2（同步采样）+ 方案1（平顶窗作为保险）
 * 先粗测频率，调整采样率实现同步，再加平顶窗进一步提高精度。
 * 
 * ============================================================
 */

#include "thd_measure.h"
#include "../config.h"
#include <math.h>
#include <stdint.h>

#ifndef float32_t
typedef float float32_t;
#endif

#define PI 3.14159265f

/* 算法层不依赖 CMSIS-DSP；用手写 Cooley-Tukey FFT。
 * 真机若需加速，定义 USE_CMSIS_DSP 宏切到 arm_cfft_f32（仅 Drivers/Core 层提供包装）。
 */

static float32_t fft_buf[FFT_SIZE * 2];
static float32_t mag_buf[FFT_SIZE];

void thd_init(void)
{
    /* naive 版本无需预算实例 */
}

/* ===== 手写 Cooley-Tukey FFT（位反转 + 蝶形）===== */
static void naive_fft_f32(float *d, uint32_t N)
{
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
        float th = -2.0f * PI / (float)m;
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

static void naive_cmag_f32(const float *cdata, float *mag, uint32_t N)
{
    for (uint32_t i = 0; i < N; i++) {
        float re = cdata[2*i], im = cdata[2*i + 1];
        mag[i] = sqrtf(re * re + im * im);
    }
}

/* ========== 第一步：粗测频率 ========== */

/**
 * 用过零检测法粗测信号频率
 * 
 * 为什么不直接用FFT测频率？
 * 因为FFT的频率分辨率取决于采样率和点数，
 * 而我们还没确定最终的采样率（需要根据频率调整）。
 * 过零检测不依赖采样率，适合做粗测。
 * 
 * @param data  ADC采样数据（去偏置后，有正有负）
 * @param len   数据长度
 * @param fs    当前采样率(Hz)
 * @return 估计的信号频率(Hz)
 */
float coarse_freq_measure(int16_t* data, uint16_t len, float fs)
{
    int zero_crossings = 0;
    int first_cross = -1, last_cross = -1;
    
    for (int i = 1; i < len; i++) {
        // 检测上升沿过零（从负到正）
        if (data[i-1] < 0 && data[i] >= 0) {
            zero_crossings++;
            if (first_cross < 0) first_cross = i;
            last_cross = i;
        }
    }
    
    if (zero_crossings < 2) return 0;  // 数据不够
    
    // 频率 = 过零次数 / 时间跨度
    // 每个上升沿过零对应一个完整周期
    float time_span = (float)(last_cross - first_cross) / fs;
    float freq = (float)(zero_crossings - 1) / time_span;
    
    return freq;
}

/* ========== 第二步：计算同步采样率 ========== */

/**
 * 计算最优采样率，使得FFT_SIZE个点恰好覆盖M个信号周期
 * 
 * 条件：fs_new = freq × FFT_SIZE / M
 * 其中M是正整数，选择使fs_new最接近初始采样率的M
 * 
 * @param freq      信号频率(Hz)
 * @param fs_init   初始采样率(Hz)
 * @return 调整后的采样率(Hz)
 */
float calc_sync_sample_rate(float freq, float fs_init)
{
    // M = round(freq × FFT_SIZE / fs_init)
    float M_float = freq * FFT_SIZE / fs_init;
    int M = (int)(M_float + 0.5f);
    if (M < 1) M = 1;
    
    float fs_new = freq * FFT_SIZE / M;
    
    return fs_new;
}

/* ========== 第三步：平顶窗 ========== */

/**
 * 平顶窗（Flat-Top Window）
 * 
 * 为什么用平顶窗而不是汉宁窗？
 * - 汉宁窗：频率分辨率好，但幅度精度差（最大误差1.42dB）
 * - 平顶窗：频率分辨率差，但幅度精度极好（最大误差<0.01dB）
 * - THD测量需要精确的幅度，所以平顶窗更合适
 * 
 * 如果已经做了同步采样，理论上不需要窗函数。
 * 但实际中频率测量有误差，同步不完美，平顶窗作为保险。
 */
static float flat_top_window[FFT_SIZE];
static float window_energy_factor = 1.0f;  // 窗函数能量补偿系数

void init_flat_top_window(void)
{
    // 平顶窗系数（HFT90D）
    const float a0 = 1.0f;
    const float a1 = 1.942604f;
    const float a2 = 1.340318f;
    const float a3 = 0.440811f;
    const float a4 = 0.043097f;
    
    float sum = 0;
    for (int i = 0; i < FFT_SIZE; i++) {
        float n = (float)i / (FFT_SIZE - 1);
        flat_top_window[i] = a0 
            - a1 * cosf(2*PI*n) 
            + a2 * cosf(4*PI*n) 
            - a3 * cosf(6*PI*n) 
            + a4 * cosf(8*PI*n);
        sum += flat_top_window[i];
    }
    
    // 能量补偿系数：使加窗后的幅度与未加窗一致
    window_energy_factor = (float)FFT_SIZE / sum;
}

/* ========== 第四步：FFT + THD计算 ========== */

/* THDResult_t 已在 thd_measure.h 中声明，本文件不再重复 */

/**
 * 完整的THD测量
 * 
 * @param adc_data  ADC采样数据(uint16_t)
 * @param len       数据长度(=FFT_SIZE)
 * @param fs        采样率(Hz)
 * @param v_scale   ADC值到电压的换算系数(V/LSB)
 * @param v_offset  ADC零点偏置(LSB)
 * @return THD测量结果
 */
THDResult_t measure_thd(uint16_t* adc_data, uint16_t len, float fs,
                         float v_scale, float v_offset)
{
    THDResult_t result = {0};
    
    // 1. 转换为浮点并去偏置
    for (int i = 0; i < FFT_SIZE; i++) {
        float v = ((float)adc_data[i] - v_offset) * v_scale;
        fft_buf[2*i] = v * flat_top_window[i];  // 加窗
        fft_buf[2*i + 1] = 0;
    }
    
    // 2. FFT
    naive_fft_f32(fft_buf, FFT_SIZE);
    
    // 3. 计算幅度谱
    naive_cmag_f32(fft_buf, mag_buf, FFT_SIZE);
    
    // 4. 归一化（除以N/2，乘以窗函数补偿）
    float norm = window_energy_factor * 2.0f / FFT_SIZE;
    for (int i = 0; i < FFT_SIZE/2; i++) {
        mag_buf[i] *= norm;
    }
    mag_buf[0] *= 0.5f;  // 直流分量只除以N
    
    // 5. 找基波（最大峰值，跳过直流）
    float max_val = 0;
    uint32_t fund_bin = 0;
    for (int i = 2; i < FFT_SIZE/2; i++) {
        if (mag_buf[i] > max_val) {
            max_val = mag_buf[i];
            fund_bin = i;
        }
    }
    
    // 6. 提取基波和谐波幅值
    // 【注意】即使做了同步采样，也要在峰值附近取最大值
    // 因为实际频率可能有微小偏差
    
    for (int h = 1; h <= 5; h++) {
        uint32_t center = fund_bin * h;
        if (center >= FFT_SIZE/2 - 1) break;
        
        // 在center±1范围内取最大值
        float peak = mag_buf[center];
        if (center > 0 && mag_buf[center-1] > peak) peak = mag_buf[center-1];
        if (center < FFT_SIZE/2-1 && mag_buf[center+1] > peak) peak = mag_buf[center+1];
        
        result.harmonic_vrms[h] = peak / 1.414f;  // 峰值→有效值
    }
    
    result.fundamental_vrms = result.harmonic_vrms[1];
    result.frequency = (float)fund_bin * fs / FFT_SIZE;
    
    // 7. 归一化幅值
    if (result.fundamental_vrms > 0.001f) {
        for (int h = 1; h <= 5; h++) {
            result.harmonic_norm[h] = result.harmonic_vrms[h] / result.fundamental_vrms;
        }
    }
    
    // 8. 计算THD
    float sum_harm_sq = 0;
    for (int h = 2; h <= 5; h++) {
        sum_harm_sq += result.harmonic_vrms[h] * result.harmonic_vrms[h];
    }
    if (result.fundamental_vrms > 0.001f) {
        result.thd_percent = sqrtf(sum_harm_sq) / result.fundamental_vrms * 100.0f;
    }
    
    return result;
}

/* ========== 完整测量流程 ========== */

/**
 * 一键测量流程（按下启动键后自动执行）
 * 
 * 1. 用初始采样率(100kHz)采集一帧数据
 * 2. 粗测频率
 * 3. 计算同步采样率
 * 4. 重新配置ADC采样率
 * 5. 采集同步数据
 * 6. FFT + THD计算
 * 7. 显示结果
 * 
 * 总时间预算：
 *   粗测采样: 1024/100000 = 10ms
 *   频率计算: <1ms
 *   重配ADC: <1ms
 *   同步采样: 1024/fs_new ≈ 10~50ms
 *   FFT计算: <5ms (STM32F4 @168MHz)
 *   显示: <50ms
 *   总计: <120ms，远小于10s限制
 */
