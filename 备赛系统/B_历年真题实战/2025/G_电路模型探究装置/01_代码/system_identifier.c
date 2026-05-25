/**
 * 2025G 电路自主学习与建模核心算法
 * 
 * ============================================================
 * 【这道题为什么是近五年的巅峰之作？】
 * ============================================================
 * 
 * 1. 它打破了电赛"给定任务→实现功能"的传统模式
 *    变成了"未知系统→自主探索→建模→推理"
 *    这是AI时代的思维方式在电赛中的首次体现
 * 
 * 2. 它要求的不是某个具体技术，而是系统级的工程能力：
 *    信号发生 + 信号采集 + 频域分析 + 模型拟合 + 实时信号合成
 *    每一环都不能出错
 * 
 * 3. 发挥部分(2)的40分要求处理正弦波、矩形波和"其他周期信号"
 *    矩形波含有无穷多次谐波，需要对每个谐波分量分别处理
 *    这要求真正理解傅里叶分析的本质
 * 
 * ============================================================
 * 【我的整体方案】
 * ============================================================
 * 
 * 学习阶段（2分钟内完成）：
 *   1. 向未知电路输入扫频正弦信号（100Hz~50kHz）
 *   2. 同时采集输入和输出信号
 *   3. 对每个频率点计算增益和相位
 *   4. 得到频率响应曲线（波特图）
 *   5. 判断滤波器类型（低通/高通/带通/带阻）
 *   6. 将频率响应数据存储为查找表
 * 
 * 推理阶段（5秒内完成）：
 *   1. 采集输入信号（来自信号发生器）
 *   2. 对输入信号做FFT，得到各频率分量
 *   3. 对每个频率分量，查找表得到增益和相位
 *   4. 将增益和相位应用到每个频率分量
 *   5. IFFT合成输出信号
 *   6. 通过DAC输出
 * 
 * ============================================================
 */

#include "stm32f4xx_hal.h"
#include "arm_math.h"
#include <math.h>

#define PI 3.14159265f
#define FFT_SIZE 2048
#define MAX_FREQ_POINTS 200   // 频率响应的采样点数

/* ========== 频率响应存储 ========== */

typedef struct {
    float frequency;    // 频率 (Hz)
    float gain;         // 增益 (线性，不是dB)
    float phase;        // 相位 (rad)
} FreqResponse_t;

static FreqResponse_t freq_resp[MAX_FREQ_POINTS];
static uint16_t freq_resp_len = 0;

typedef enum {
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_BANDSTOP,
    FILTER_UNKNOWN
} FilterType_t;

static FilterType_t identified_type = FILTER_UNKNOWN;

/* ========== 学习阶段：扫频测量 ========== */

/**
 * 【扫频策略的深层思考】
 * 
 * 简单做法：线性扫频，100Hz步进，100Hz~50kHz = 500个频率点
 * 问题：每个频率点需要等待信号稳定（至少5个周期），
 *       低频(100Hz)需要50ms，高频(50kHz)需要0.1ms
 *       总时间 ≈ 500 × 50ms = 25秒 → 超过2分钟限制？不会，
 *       但低频点太多浪费时间。
 * 
 * 更好的做法：对数扫频
 *   低频段(100Hz~1kHz)：步进100Hz，10个点
 *   中频段(1kHz~10kHz)：步进500Hz，18个点
 *   高频段(10kHz~50kHz)：步进2kHz，20个点
 *   总共约50个点，每个点等待10个周期
 *   总时间 ≈ 50 × (10/freq) 的求和 ≈ 2秒
 *   远小于2分钟限制！
 * 
 * 【另一个关键】等待时间
 * 切换频率后，电路需要时间达到稳态。
 * 等待时间 = 电路时间常数的5倍
 * 对于RLC电路，时间常数 τ = 2L/R 或 RC
 * R=1~10kΩ, C=10~100nF → τ = 10μs~1ms
 * 5τ = 50μs~5ms
 * 加上采集10个周期的时间，每个频率点总共约10~50ms
 */

// DDS输出频率设置（假设用AD9833）
extern void DDS_SetFreq(float freq_hz);
extern void DDS_SetAmplitude(float vpp);

// ADC采样（双通道同步：输入信号+输出信号）
extern void ADC_SyncSample(float* input_buf, float* output_buf, 
                            uint16_t len, float sample_rate);

/**
 * 测量单个频率点的增益和相位
 * 
 * 方法：对输入和输出信号分别做DFT，提取基波分量，
 *       增益 = |Vout| / |Vin|
 *       相位 = ∠Vout - ∠Vin
 * 
 * 【为什么用DFT而不是简单的峰值比？】
 * 峰值比只能得到增益，得不到相位。
 * 而相位信息对于推理阶段至关重要——
 * 没有相位，矩形波的重建会完全错误。
 */
FreqResponse_t measure_single_freq(float freq_hz)
{
    FreqResponse_t resp;
    resp.frequency = freq_hz;
    
    // 1. 设置DDS输出频率
    DDS_SetFreq(freq_hz);
    DDS_SetAmplitude(2.0f);  // 2Vpp
    
    // 2. 等待稳态
    float wait_ms = 10.0f / freq_hz * 1000.0f;  // 10个周期
    if (wait_ms < 5.0f) wait_ms = 5.0f;         // 最少5ms
    HAL_Delay((uint32_t)wait_ms);
    
    // 3. 采样（采集恰好10个周期的数据）
    uint16_t num_samples = 512;  // 固定采样点数
    float sample_rate = freq_hz * num_samples / 10.0f;  // 确保10个周期
    if (sample_rate > 1000000) sample_rate = 1000000;    // 限制最高1MSPS
    
    static float in_buf[512], out_buf[512];
    ADC_SyncSample(in_buf, out_buf, num_samples, sample_rate);
    
    // 4. DFT提取基波（只算一个频率点，不需要完整FFT）
    float in_sin = 0, in_cos = 0;
    float out_sin = 0, out_cos = 0;
    
    // 基波在bin = 10（因为采了10个周期）
    for (int i = 0; i < num_samples; i++) {
        float angle = 2.0f * PI * 10.0f * i / num_samples;
        float s = arm_sin_f32(angle);
        float c = arm_cos_f32(angle);
        
        in_sin  += in_buf[i] * s;
        in_cos  += in_buf[i] * c;
        out_sin += out_buf[i] * s;
        out_cos += out_buf[i] * c;
    }
    
    float in_mag  = sqrtf(in_sin*in_sin + in_cos*in_cos);
    float out_mag = sqrtf(out_sin*out_sin + out_cos*out_cos);
    float in_phase  = atan2f(in_sin, in_cos);
    float out_phase = atan2f(out_sin, out_cos);
    
    // 5. 计算增益和相位差
    resp.gain = (in_mag > 0.001f) ? out_mag / in_mag : 0;
    resp.phase = out_phase - in_phase;
    
    // 相位归一化到[-π, π]
    while (resp.phase > PI) resp.phase -= 2*PI;
    while (resp.phase < -PI) resp.phase += 2*PI;
    
    return resp;
}

/**
 * 完整扫频测量
 */
void sweep_and_learn(void)
{
    freq_resp_len = 0;
    
    // 对数分布的频率点
    float freqs[] = {
        100, 200, 300, 500, 700,
        1000, 1500, 2000, 3000, 4000, 5000, 7000,
        10000, 15000, 20000, 25000, 30000, 35000, 40000, 45000, 50000
    };
    int num_freqs = sizeof(freqs) / sizeof(freqs[0]);
    
    for (int i = 0; i < num_freqs && freq_resp_len < MAX_FREQ_POINTS; i++) {
        freq_resp[freq_resp_len] = measure_single_freq(freqs[i]);
        freq_resp_len++;
    }
    
    // 判断滤波器类型
    identified_type = identify_filter_type();
}

/* ========== 滤波器类型识别 ========== */

/**
 * 【识别策略】
 * 
 * 低通：低频增益高，高频增益低
 * 高通：低频增益低，高频增益高
 * 带通：中间频率增益高，两端低
 * 带阻：中间频率增益低，两端高
 * 
 * 具体判断：比较低频段(前1/3)、中频段(中间1/3)、高频段(后1/3)的平均增益
 */
FilterType_t identify_filter_type(void)
{
    if (freq_resp_len < 6) return FILTER_UNKNOWN;
    
    int third = freq_resp_len / 3;
    
    float gain_low = 0, gain_mid = 0, gain_high = 0;
    for (int i = 0; i < third; i++) gain_low += freq_resp[i].gain;
    for (int i = third; i < 2*third; i++) gain_mid += freq_resp[i].gain;
    for (int i = 2*third; i < freq_resp_len; i++) gain_high += freq_resp[i].gain;
    
    gain_low /= third;
    gain_mid /= third;
    gain_high /= (freq_resp_len - 2*third);
    
    // 找最大和最小
    float max_gain = fmaxf(fmaxf(gain_low, gain_mid), gain_high);
    float min_gain = fminf(fminf(gain_low, gain_mid), gain_high);
    
    if (gain_low > gain_high * 2) return FILTER_LOWPASS;
    if (gain_high > gain_low * 2) return FILTER_HIGHPASS;
    if (gain_mid > gain_low * 1.5f && gain_mid > gain_high * 1.5f) return FILTER_BANDPASS;
    if (gain_mid < gain_low * 0.7f && gain_mid < gain_high * 0.7f) return FILTER_BANDSTOP;
    
    return FILTER_UNKNOWN;
}

/* ========== 推理阶段：频域处理 ========== */

/**
 * 【推理的核心思想】
 * 
 * 任何周期信号都可以分解为正弦分量的叠加（傅里叶级数）。
 * 线性系统对每个正弦分量独立作用（叠加原理）。
 * 所以：
 *   1. 把输入信号FFT分解为各频率分量
 *   2. 对每个分量，查频率响应表得到增益和相位
 *   3. 将增益和相位应用到该分量
 *   4. IFFT合成输出信号
 * 
 * 这对正弦波、矩形波、三角波、任意周期信号都适用！
 * 
 * 【为什么这个方法比拟合传递函数更好？】
 * 
 * 方案A：拟合传递函数H(s)的参数（R,L,C值）
 *   优点：模型紧凑
 *   缺点：拟合可能不准，特别是高阶系统
 *   
 * 方案B：直接用频率响应查找表（我的选择）
 *   优点：不需要拟合，测量多准就多准
 *   缺点：只在测量过的频率点上准确，中间需要插值
 *   
 * 对于本题，未知电路只有R+L+C各一个（最多二阶），
 * 方案A理论上可行。但方案B更通用、更鲁棒，
 * 即使电路更复杂也能工作。
 */

static arm_cfft_instance_f32 fft_inst;
static float fft_buf[FFT_SIZE * 2];

void inference_init(void)
{
    arm_cfft_init_f32(&fft_inst, FFT_SIZE);
}

/**
 * 在频率响应表中插值，得到任意频率的增益和相位
 */
void interpolate_response(float freq, float* gain, float* phase)
{
    // 边界处理
    if (freq <= freq_resp[0].frequency) {
        *gain = freq_resp[0].gain;
        *phase = freq_resp[0].phase;
        return;
    }
    if (freq >= freq_resp[freq_resp_len-1].frequency) {
        *gain = freq_resp[freq_resp_len-1].gain;
        *phase = freq_resp[freq_resp_len-1].phase;
        return;
    }
    
    // 找到freq所在的区间
    for (int i = 0; i < freq_resp_len - 1; i++) {
        if (freq >= freq_resp[i].frequency && freq < freq_resp[i+1].frequency) {
            // 线性插值
            float t = (freq - freq_resp[i].frequency) / 
                      (freq_resp[i+1].frequency - freq_resp[i].frequency);
            *gain = freq_resp[i].gain * (1-t) + freq_resp[i+1].gain * t;
            *phase = freq_resp[i].phase * (1-t) + freq_resp[i+1].phase * t;
            return;
        }
    }
    
    *gain = 1.0f;
    *phase = 0;
}

/**
 * 推理：根据输入信号生成与未知电路相同的输出
 * 
 * @param input   输入信号（来自信号发生器，通过ADC采集）
 * @param output  输出信号（写入DAC缓冲区）
 * @param N       信号长度
 * @param fs      采样率
 */
void inference_generate(float* input, float* output, uint16_t N, float fs)
{
    // 1. FFT
    for (int i = 0; i < N; i++) {
        fft_buf[2*i] = input[i];
        fft_buf[2*i+1] = 0;
    }
    arm_cfft_f32(&fft_inst, fft_buf, 0, 1);
    
    // 2. 对每个频率分量应用频率响应
    float freq_resolution = fs / N;
    
    for (int k = 0; k < N; k++) {
        float freq;
        if (k <= N/2) {
            freq = k * freq_resolution;
        } else {
            freq = (k - N) * freq_resolution;  // 负频率
        }
        freq = fabsf(freq);
        
        float gain, phase;
        interpolate_response(freq, &gain, &phase);
        
        // 应用增益和相位
        // H(f) = gain × exp(j×phase)
        // Y(f) = H(f) × X(f)
        float xr = fft_buf[2*k];
        float xi = fft_buf[2*k+1];
        float hr = gain * arm_cos_f32(phase);
        float hi = gain * arm_sin_f32(phase);
        
        // 复数乘法
        fft_buf[2*k]   = xr * hr - xi * hi;
        fft_buf[2*k+1] = xr * hi + xi * hr;
    }
    
    // 3. IFFT
    arm_cfft_f32(&fft_inst, fft_buf, 1, 1);
    
    // 4. 取实部作为输出
    for (int i = 0; i < N; i++) {
        output[i] = fft_buf[2*i];
    }
}
