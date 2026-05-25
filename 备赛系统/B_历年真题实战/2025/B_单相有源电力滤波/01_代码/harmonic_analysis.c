/**
 * 2025B 谐波分析模块
 * 功能：对负载电流iL做FFT，提取基波和各次谐波幅值，计算THD和Hi
 * 
 * 这是基本要求(2)(3)的核心代码，占42分
 */

#include "arm_math.h"
#include <math.h>
#include <stdio.h>

#define FFT_SIZE        1024
#define SAMPLE_RATE     10000.0f   // 10kHz采样率 (50Hz基波的200倍)
#define FUND_FREQ       50.0f     // 基波频率
#define FUND_INDEX      (uint32_t)(FUND_FREQ * FFT_SIZE / SAMPLE_RATE)  // =5

// FFT缓冲区
static float32_t fft_input[FFT_SIZE * 2];   // 复数 (实部+虚部交替)
static float32_t fft_mag[FFT_SIZE];          // 幅度谱

// 谐波分析结果
typedef struct {
    float i_rms;           // 电流有效值 (A)
    float i_fund_rms;      // 基波有效值 (A)
    float harmonic[11];    // 各次谐波有效值 [0]=DC, [1]=基波, [2]=2次, ..., [10]=10次
    float Hi[11];          // 各次谐波含有率 (%) Hi = I_i / I_1 * 100
    float thd;             // 总谐波失真 (%)
} HarmonicResult_t;

static arm_cfft_instance_f32 fft_inst;
static HarmonicResult_t result;

void Harmonic_Init(void)
{
    arm_cfft_init_f32(&fft_inst, FFT_SIZE);
}

/**
 * 对ADC采样数据进行谐波分析
 * @param adc_data  ADC原始数据 (uint16_t数组)
 * @param len       数据长度 (应等于FFT_SIZE)
 * @param scale     ADC值到电流的换算系数 (A/LSB)
 * @param offset    ADC零点偏置 (LSB)
 * @return 谐波分析结果指针
 */
HarmonicResult_t* Harmonic_Analyze(uint16_t* adc_data, uint16_t len, 
                                     float scale, float offset)
{
    // ===== 1. 数据预处理 =====
    float sum_sq = 0;
    for (int i = 0; i < FFT_SIZE; i++) {
        float current = ((float)adc_data[i] - offset) * scale;
        fft_input[2*i] = current;      // 实部
        fft_input[2*i + 1] = 0;        // 虚部
        sum_sq += current * current;
    }
    
    // 计算电流有效值 (时域法，作为校验)
    result.i_rms = sqrtf(sum_sq / FFT_SIZE);
    
    // ===== 2. 加汉宁窗 (提高幅度精度) =====
    for (int i = 0; i < FFT_SIZE; i++) {
        float w = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (FFT_SIZE - 1)));
        fft_input[2*i] *= w;
    }
    
    // ===== 3. 执行FFT =====
    arm_cfft_f32(&fft_inst, fft_input, 0, 1);
    
    // ===== 4. 计算幅度谱 =====
    arm_cmplx_mag_f32(fft_input, fft_mag, FFT_SIZE);
    
    // ===== 5. 提取各次谐波 =====
    // 汉宁窗校正系数: 幅度需要乘以2(窗函数能量补偿)
    // 归一化: 除以N/2
    float norm_factor = 2.0f / FFT_SIZE * 2.0f;  // 2/N * 窗校正2
    
    for (int h = 0; h <= 10; h++) {
        uint32_t idx = FUND_INDEX * h;
        if (idx < FFT_SIZE / 2) {
            // 取峰值附近3个bin的最大值 (防止频谱泄漏导致偏移)
            float max_val = fft_mag[idx];
            if (idx > 0 && fft_mag[idx-1] > max_val) max_val = fft_mag[idx-1];
            if (idx < FFT_SIZE/2 - 1 && fft_mag[idx+1] > max_val) max_val = fft_mag[idx+1];
            
            result.harmonic[h] = max_val * norm_factor / 1.414f;  // 峰值→有效值
        } else {
            result.harmonic[h] = 0;
        }
    }
    
    result.i_fund_rms = result.harmonic[1];
    
    // ===== 6. 计算谐波含有率 Hi =====
    if (result.i_fund_rms > 0.001f) {
        for (int h = 2; h <= 10; h++) {
            result.Hi[h] = result.harmonic[h] / result.i_fund_rms * 100.0f;
        }
    }
    
    // ===== 7. 计算THD =====
    float sum_harm_sq = 0;
    for (int h = 2; h <= 10; h++) {
        sum_harm_sq += result.harmonic[h] * result.harmonic[h];
    }
    if (result.i_fund_rms > 0.001f) {
        result.thd = sqrtf(sum_harm_sq) / result.i_fund_rms * 100.0f;
    } else {
        result.thd = 0;
    }
    
    return &result;
}

/**
 * 格式化显示谐波分析结果
 * 用于LCD显示或串口打印
 */
void Harmonic_PrintResult(HarmonicResult_t* r)
{
    printf("Irms=%.3fA  I1=%.3fA  THD=%.1f%%\r\n", 
           r->i_rms, r->i_fund_rms, r->thd);
    printf("H2=%.1f%% H3=%.1f%% H4=%.1f%% H5=%.1f%%\r\n",
           r->Hi[2], r->Hi[3], r->Hi[4], r->Hi[5]);
}
