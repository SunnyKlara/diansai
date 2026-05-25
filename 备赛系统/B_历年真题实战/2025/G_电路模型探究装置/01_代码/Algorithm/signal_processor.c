/**
 * 信号处理模块 —— 实现
 * 
 * ============================================================
 * 【这个模块是整道题的灵魂】
 * ============================================================
 * 
 * 它做的事情用一句话概括：
 * "把输入信号的每个频率分量，按照未知电路的频率响应进行缩放和移相"
 * 
 * 数学表达：
 *   Y(f) = H(f) × X(f)
 *   其中 X(f) = FFT(输入信号)
 *        H(f) = 频率响应（学习阶段测量的）
 *        Y(f) = FFT(输出信号)
 *   输出信号 = IFFT(Y(f))
 * 
 * 这就是线性时不变(LTI)系统的核心性质：
 * 频域中的乘法 = 时域中的卷积。
 * 
 * 【为什么这对任意周期信号都有效？】
 * 
 * 正弦波：只有一个频率分量，查一次表
 * 矩形波：有无穷多个谐波，每个谐波独立查表
 * 三角波：同上
 * 任意波形：同上
 * 
 * 只要系统是线性的（RLC电路是线性的），这个方法就精确。
 * 
 * ============================================================
 * 【实现中的关键细节】
 * ============================================================
 * 
 * 1. FFT的频率bin和实际频率的对应关系
 *    bin k 对应的频率 = k × fs / N
 *    bin 0 = 直流(0Hz)
 *    bin 1 = fs/N
 *    bin N/2 = fs/2 (奈奎斯特频率)
 *    bin N/2+1 ~ N-1 = 负频率（共轭对称）
 * 
 * 2. 负频率的处理
 *    实信号的FFT具有共轭对称性：X(N-k) = conj(X(k))
 *    所以只需要处理0~N/2的正频率，负频率取共轭即可。
 *    但在应用H(f)时，正负频率都要处理！
 *    H(-f) = conj(H(f))（实系统的频率响应也是共轭对称的）
 * 
 * 3. 复数乘法
 *    Y = H × X
 *    (Yr + jYi) = (Hr + jHi) × (Xr + jXi)
 *    Yr = Hr×Xr - Hi×Xi
 *    Yi = Hr×Xi + Hi×Xr
 * 
 * ============================================================
 */

#include "signal_processor.h"
#include "freq_response.h"
#include "arm_math.h"
#include <math.h>

#define PI 3.14159265f

static arm_cfft_instance_f32 fft_instance;
static float fft_buf[FFT_SIZE * 2];  // 复数缓冲区（实部+虚部交替）

void SigProc_Init(void)
{
    arm_cfft_init_f32(&fft_instance, FFT_SIZE);
}

void SigProc_ProcessFrame(int16_t* input, uint16_t* output, uint16_t length, float fs)
{
    if (length != FFT_SIZE) return;  // 安全检查
    
    float freq_resolution = fs / (float)FFT_SIZE;
    
    // ===== 1. 输入数据转换：int16 → float复数 =====
    /*
     * ADC数据范围：-2048 ~ +2047（已去偏置）
     * 归一化到±1.0范围，方便后续处理
     * 虚部填0（实信号）
     */
    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        fft_buf[2 * i]     = (float)input[i] / 2048.0f;  // 实部
        fft_buf[2 * i + 1] = 0;                            // 虚部
    }
    
    // ===== 2. FFT =====
    arm_cfft_f32(&fft_instance, fft_buf, 0, 1);  // 0=正变换, 1=位反转
    
    // ===== 3. 频域处理：对每个bin应用频率响应 =====
    for (uint16_t k = 0; k <= FFT_SIZE / 2; k++) {
        float freq = (float)k * freq_resolution;
        
        // 查频率响应表
        float gain, phase;
        FreqResp_Interpolate(freq, &gain, &phase);
        
        // 构造H(f)的复数形式
        // H = gain × exp(j × phase) = gain×cos(phase) + j×gain×sin(phase)
        float hr = gain * arm_cos_f32(phase);
        float hi = gain * arm_sin_f32(phase);
        
        // 读取X(f)
        float xr = fft_buf[2 * k];
        float xi = fft_buf[2 * k + 1];
        
        // 复数乘法 Y = H × X
        fft_buf[2 * k]     = hr * xr - hi * xi;
        fft_buf[2 * k + 1] = hr * xi + hi * xr;
        
        // 处理共轭对称的负频率（k > 0 且 k < N/2）
        if (k > 0 && k < FFT_SIZE / 2) {
            uint16_t neg_k = FFT_SIZE - k;
            
            // 负频率：H(-f) = conj(H(f))，X(-f) = conj(X(f))
            // Y(-f) = H(-f) × X(-f) = conj(H(f)) × conj(X(f)) = conj(H(f) × X(f)) = conj(Y(f))
            // 所以直接取Y(k)的共轭
            fft_buf[2 * neg_k]     =  fft_buf[2 * k];      // 实部相同
            fft_buf[2 * neg_k + 1] = -fft_buf[2 * k + 1];  // 虚部取反
        }
    }
    
    // ===== 4. IFFT =====
    arm_cfft_f32(&fft_instance, fft_buf, 1, 1);  // 1=逆变换
    
    // ===== 5. 输出数据转换：float → uint16 (DAC值) =====
    /*
     * IFFT结果是实数（虚部应该接近0，因为输入是实信号）
     * 取实部，反归一化，加偏置2048，限幅到0~4095
     * 
     * 【增益校准】
     * CMSIS-DSP的FFT/IFFT不做1/N归一化，
     * 所以IFFT的结果需要除以N。
     * 但实际中，由于频率响应的增益已经包含了幅度信息，
     * 可能需要额外的缩放因子。
     * 这个因子在调试时通过比较输出幅度和预期值来确定。
     */
    float scale = 2048.0f / (float)FFT_SIZE;  // 反归一化 + IFFT归一化
    
    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        float val = fft_buf[2 * i] * scale;  // 取实部
        
        // 加偏置（DAC输出范围0~4095，中间值2048对应0V）
        int32_t dac_val = (int32_t)(val + 2048.0f + 0.5f);
        
        // 限幅
        if (dac_val > 4095) dac_val = 4095;
        if (dac_val < 0) dac_val = 0;
        
        output[i] = (uint16_t)dac_val;
    }
}
