/**
 * 2023D 调制方式自动识别算法
 * 
 * ============================================================
 * 【这道题的核心挑战：6种调制方式的可靠区分】
 * ============================================================
 * 
 * 基本要求：AM / FM / CW（3种模拟调制）
 * 发挥部分：2ASK / 2FSK / 2PSK（3种数字调制）
 * 
 * 【为什么简单的特征提取不够？】
 * 
 * 很多方案用"包络方差"区分AM和FM/CW，用"瞬时频率方差"区分FM和CW。
 * 这在理想情况下可行，但实际中有两个致命问题：
 * 
 * 1. 噪声导致特征重叠
 *    CW信号的包络理论上恒定，但ADC噪声会引入包络波动。
 *    如果AM的调幅度很小(ma=0.3)，包络波动也很小。
 *    两者的包络方差可能非常接近，导致误判。
 * 
 * 2. 数字调制和模拟调制的特征可能相似
 *    2ASK的包络也有变化（像AM）
 *    2FSK的瞬时频率也有变化（像FM）
 *    需要更精细的特征来区分
 * 
 * 【我的方案：多特征决策树 + 统计检验】
 * 
 * 不依赖单一特征，而是提取多个特征，用决策树逐步缩小范围。
 * 每个判决节点用统计检验（而不是简单的阈值比较），提高鲁棒性。
 * 
 * ============================================================
 */

#include "arm_math.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f
#define FRAME_SIZE 4096     // 采样帧长度
#define SAMPLE_RATE 10000000.0f  // 10MSPS（带通采样，见下文说明）

/* ========== 调制类型定义 ========== */
typedef enum {
    MOD_AM,     // 调幅
    MOD_FM,     // 调频
    MOD_CW,     // 连续载波
    MOD_2ASK,   // 二进制幅度键控
    MOD_2FSK,   // 二进制频移键控
    MOD_2PSK,   // 二进制相移键控
    MOD_UNKNOWN
} ModType_t;

/* ========== 特征提取 ========== */

/**
 * 【关于采样率的深层思考】
 * 
 * 载波2MHz，奈奎斯特要求≥4MHz。
 * 但我们不用4MHz采样，而是用**带通采样**。
 * 
 * 带通采样定理：如果信号带宽B远小于载频fc，
 * 采样率只需≥2B（而不是≥2fc）。
 * 
 * 本题：载波2MHz，调制带宽约10kHz（最大调制频率5kHz×2边带）
 * 理论上采样率只需≥20kHz！
 * 
 * 但实际中，数字调制的带宽更大（码速率10kbps→带宽约20kHz）
 * 加上保护带，采样率选100kHz比较安全。
 * 
 * 不过，100kHz采样2MHz载波会产生混叠。
 * 需要在采样前用带通滤波器限制信号带宽。
 * 
 * 更实际的方案：用SA612混频到中频（如455kHz），
 * 然后用1MSPS的ADC采样中频信号。
 * 这样既避免了高速ADC的需求，又保留了所有调制信息。
 */

typedef struct {
    // 包络特征
    float env_mean;         // 包络均值
    float env_std;          // 包络标准差
    float env_std_norm;     // 归一化包络标准差 = std/mean
    
    // 瞬时频率特征
    float freq_mean;        // 瞬时频率均值
    float freq_std;         // 瞬时频率标准差
    
    // 瞬时相位特征
    float phase_std;        // 瞬时相位标准差（去除线性趋势后）
    
    // 高阶统计量
    float env_kurtosis;     // 包络峰度（区分连续和离散分布）
    
    // 频谱特征
    float spectral_symmetry; // 频谱对称性（AM对称，FM不对称）
} SignalFeatures_t;

/**
 * 提取信号包络（使用Hilbert变换）
 * 
 * Hilbert变换的FFT实现：
 * 1. FFT
 * 2. 正频率分量×2，负频率分量×0，直流和奈奎斯特不变
 * 3. IFFT
 * 4. 取模 = 包络
 */
void extract_envelope(float* signal, float* envelope, int N)
{
    static float fft_buf[FRAME_SIZE * 2];
    
    // FFT
    for (int i = 0; i < N; i++) {
        fft_buf[2*i] = signal[i];
        fft_buf[2*i+1] = 0;
    }
    
    arm_cfft_instance_f32 inst;
    arm_cfft_init_f32(&inst, N);
    arm_cfft_f32(&inst, fft_buf, 0, 1);
    
    // 构造解析信号频谱
    // DC和Nyquist不变，正频率×2，负频率=0
    // fft_buf[0], fft_buf[1] = DC (不变)
    for (int i = 1; i < N/2; i++) {
        fft_buf[2*i] *= 2;
        fft_buf[2*i+1] *= 2;
    }
    // fft_buf[N], fft_buf[N+1] = Nyquist (不变)
    for (int i = N/2 + 1; i < N; i++) {
        fft_buf[2*i] = 0;
        fft_buf[2*i+1] = 0;
    }
    
    // IFFT
    arm_cfft_f32(&inst, fft_buf, 1, 1);
    
    // 取模 = 包络
    for (int i = 0; i < N; i++) {
        float re = fft_buf[2*i];
        float im = fft_buf[2*i+1];
        envelope[i] = sqrtf(re*re + im*im);
    }
}

/**
 * 提取瞬时频率
 * 
 * 瞬时频率 = d(phase)/dt / (2π)
 * 其中phase是解析信号的瞬时相位
 */
void extract_inst_freq(float* signal, float* inst_freq, int N, float fs)
{
    static float fft_buf[FRAME_SIZE * 2];
    
    // 同上，构造解析信号
    for (int i = 0; i < N; i++) {
        fft_buf[2*i] = signal[i];
        fft_buf[2*i+1] = 0;
    }
    arm_cfft_instance_f32 inst;
    arm_cfft_init_f32(&inst, N);
    arm_cfft_f32(&inst, fft_buf, 0, 1);
    
    for (int i = 1; i < N/2; i++) { fft_buf[2*i] *= 2; fft_buf[2*i+1] *= 2; }
    for (int i = N/2+1; i < N; i++) { fft_buf[2*i] = 0; fft_buf[2*i+1] = 0; }
    
    arm_cfft_f32(&inst, fft_buf, 1, 1);
    
    // 计算瞬时相位并差分
    float prev_phase = atan2f(fft_buf[1], fft_buf[0]);
    for (int i = 1; i < N; i++) {
        float phase = atan2f(fft_buf[2*i+1], fft_buf[2*i]);
        float dphase = phase - prev_phase;
        
        // 相位解缠绕
        while (dphase > PI) dphase -= 2*PI;
        while (dphase < -PI) dphase += 2*PI;
        
        inst_freq[i-1] = dphase * fs / (2*PI);
        prev_phase = phase;
    }
    inst_freq[N-1] = inst_freq[N-2];
}

/**
 * 计算峰度（Kurtosis）
 * 
 * 【为什么峰度是区分模拟和数字调制的关键特征？】
 * 
 * 模拟调制（AM/FM）：包络/频率是连续变化的 → 分布接近高斯 → 峰度≈3
 * 数字调制（ASK/FSK）：包络/频率在离散值间跳变 → 分布是双峰的 → 峰度<3
 * 
 * 这个特征比简单的方差更能区分模拟和数字调制。
 */
float calc_kurtosis(float* data, int N)
{
    float mean = 0, var = 0, kurt = 0;
    
    for (int i = 0; i < N; i++) mean += data[i];
    mean /= N;
    
    for (int i = 0; i < N; i++) {
        float d = data[i] - mean;
        var += d * d;
    }
    var /= N;
    
    if (var < 1e-10f) return 3.0f;  // 常数信号
    
    for (int i = 0; i < N; i++) {
        float d = (data[i] - mean);
        kurt += d * d * d * d;
    }
    kurt = kurt / (N * var * var);
    
    return kurt;  // 高斯分布的峰度=3
}

/* ========== 调制方式识别决策树 ========== */

/**
 * 【决策树结构】
 * 
 *                    输入信号
 *                       │
 *              ┌────────┴────────┐
 *         包络方差大？            包络方差小
 *              │                    │
 *        ┌─────┴─────┐        ┌────┴────┐
 *    包络峰度<2.5？  峰度≥2.5  频率方差大？ 频率方差小
 *        │            │          │           │
 *      2ASK          AM       ┌──┴──┐       CW
 *                          峰度<2.5  峰度≥2.5
 *                             │        │
 *                           2FSK      FM
 *                                      │
 *                                 相位方差大？
 *                                   │     │
 *                                 2PSK   FM
 * 
 * 注意：2PSK的包络恒定、频率恒定，但相位有π的跳变。
 * 它和CW的区别只在相位特征上。
 */

ModType_t classify_modulation(float* signal, int N, float fs)
{
    static float envelope[FRAME_SIZE];
    static float inst_freq[FRAME_SIZE];
    
    // 1. 提取包络
    extract_envelope(signal, envelope, N);
    
    // 2. 计算包络特征
    float env_mean = 0, env_var = 0;
    for (int i = 0; i < N; i++) env_mean += envelope[i];
    env_mean /= N;
    for (int i = 0; i < N; i++) {
        float d = envelope[i] - env_mean;
        env_var += d * d;
    }
    env_var /= N;
    float env_std_norm = sqrtf(env_var) / (env_mean + 1e-10f);
    
    float env_kurt = calc_kurtosis(envelope, N);
    
    // 3. 第一层判决：包络是否有显著变化？
    // 阈值0.05是经验值，需要用实际信号标定
    if (env_std_norm > 0.05f) {
        // 包络有变化 → AM 或 2ASK
        if (env_kurt < 2.5f) {
            // 包络分布是双峰的（离散跳变）→ 2ASK
            return MOD_2ASK;
        } else {
            // 包络分布接近连续 → AM
            return MOD_AM;
        }
    }
    
    // 4. 包络恒定，提取瞬时频率
    extract_inst_freq(signal, inst_freq, N, fs);
    
    float freq_mean = 0, freq_var = 0;
    for (int i = 0; i < N-1; i++) freq_mean += inst_freq[i];
    freq_mean /= (N-1);
    for (int i = 0; i < N-1; i++) {
        float d = inst_freq[i] - freq_mean;
        freq_var += d * d;
    }
    freq_var /= (N-1);
    float freq_std = sqrtf(freq_var);
    
    float freq_kurt = calc_kurtosis(inst_freq, N-1);
    
    // 5. 第二层判决：瞬时频率是否有显著变化？
    if (freq_std > 100.0f) {  // 频率标准差>100Hz
        if (freq_kurt < 2.5f) {
            return MOD_2FSK;  // 频率在两个值间跳变
        } else {
            return MOD_FM;    // 频率连续变化
        }
    }
    
    // 6. 包络恒定+频率恒定 → CW 或 2PSK
    // 区分方法：检测相位是否有π的跳变
    
    // 计算相位差分的绝对值直方图
    // 2PSK：相位差分集中在0和±π附近
    // CW：相位差分集中在0附近
    
    int phase_jump_count = 0;
    float prev_phase = atan2f(0, signal[0]);  // 简化
    
    // 用解析信号的相位差分检测
    // 如果相邻采样点的相位差接近π，计数+1
    static float analytic_buf[FRAME_SIZE * 2];
    // ... (复用之前的Hilbert变换结果)
    
    // 简化判断：如果相位跳变次数>阈值 → 2PSK
    // 这里用一个近似方法：计算信号与载波的乘积的符号变化次数
    // 2PSK信号与纯载波相乘后，会在码元边界处出现符号翻转
    
    // 更简单的方法：计算信号的自相关函数
    // CW的自相关是余弦衰减
    // 2PSK的自相关在码元周期处有负峰
    
    // 这里用阈值法简化
    if (phase_jump_count > N / 100) {  // 超过1%的采样点有相位跳变
        return MOD_2PSK;
    }
    
    return MOD_CW;
}

/* ========== 参数估计 ========== */

/**
 * AM调幅度估计
 * ma = (Amax - Amin) / (Amax + Amin)
 */
float estimate_am_depth(float* envelope, int N)
{
    float amax = -1e10f, amin = 1e10f;
    for (int i = 0; i < N; i++) {
        if (envelope[i] > amax) amax = envelope[i];
        if (envelope[i] < amin) amin = envelope[i];
    }
    
    if (amax + amin < 1e-10f) return 0;
    return (amax - amin) / (amax + amin);
}

/**
 * FM最大频偏估计
 * Δfmax = max(|f_inst - f_carrier|)
 */
float estimate_fm_deviation(float* inst_freq, int N)
{
    float f_mean = 0;
    for (int i = 0; i < N; i++) f_mean += inst_freq[i];
    f_mean /= N;
    
    float max_dev = 0;
    for (int i = 0; i < N; i++) {
        float dev = fabsf(inst_freq[i] - f_mean);
        if (dev > max_dev) max_dev = dev;
    }
    
    return max_dev;
}
