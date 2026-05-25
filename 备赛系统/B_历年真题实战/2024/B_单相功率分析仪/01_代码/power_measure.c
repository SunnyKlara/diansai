/**
 * 2024B 单相功率分析仪 - 功率测量核心模块
 * MCU: TI MSP430FR5994 (超低功耗)
 * 
 * 测量参数：
 *   - 电压有效值 Urms
 *   - 电流有效值 Irms
 *   - 有功功率 P
 *   - 视在功率 S
 *   - 功率因数 PF
 *   - 电流THD
 *   - 电流各次谐波(1~10次)有效值
 * 
 * 采样方案：
 *   - 采样率: 10kHz (50Hz基波的200倍，覆盖10次谐波)
 *   - 每周期采样点: 200点
 *   - ADC: MSP430片内12bit SAR ADC
 *   - 双通道同步采样: 电压+电流
 */

#include <msp430.h>
#include <math.h>
#include <stdint.h>

/* ========== 参数 ========== */
#define SAMPLE_RATE     10000   // 10kHz
#define SAMPLES_PER_CYC 200     // 每周期200点
#define FFT_SIZE        256     // FFT点数(取2的幂，≥200)
#define NUM_HARMONICS   10      // 分析到10次谐波

/* ========== 采样缓冲区 ========== */
static int16_t voltage_buf[FFT_SIZE];  // 电压采样(去偏置后)
static int16_t current_buf[FFT_SIZE];  // 电流采样(去偏置后)
static volatile uint16_t sample_idx = 0;
static volatile uint8_t cycle_complete = 0;

/* ========== 测量结果 ========== */
typedef struct {
    float v_rms;            // 电压有效值 (V)
    float i_rms;            // 电流有效值 (A)
    float power_active;     // 有功功率 (W)
    float power_apparent;   // 视在功率 (VA)
    float power_factor;     // 功率因数
    float i_harmonic[NUM_HARMONICS+1]; // 各次谐波电流有效值 [1]=基波
    float i_thd;            // 电流THD (%)
} PowerResult_t;

static PowerResult_t result;

/* ========== 缩放系数 ========== */
// 电压: 互感器变比 + 分压 → ADC
// 假设电压互感器变比1:1，分压1:100，ADC 3.3V/4096
// 实际电压 = ADC值 / 4096 * 3.3 * 100
#define V_SCALE  (3.3f / 4096.0f * 100.0f)

// 电流: 电流互感器 ZMCT103C, 变比1000:1, 采样电阻100Ω
// 互感器输出电流 = 原方电流/1000
// 采样电压 = 输出电流 × 100Ω = 原方电流 × 0.1
// 原方电流 = 采样电压 / 0.1
// 如果原方绕n匝: 原方电流 = 采样电压 / (0.1 × n)
#define I_SCALE  (3.3f / 4096.0f / 0.1f)  // n=1匝时

/* ========== ADC中断 - 10kHz采样 ========== */
// 在Timer触发的ADC中断中采集电压和电流
#pragma vector=ADC12_B_VECTOR
__interrupt void ADC12_ISR(void)
{
    if (sample_idx < FFT_SIZE) {
        // 读取双通道ADC值(去除2048偏置)
        voltage_buf[sample_idx] = (int16_t)ADC12MEM0 - 2048;
        current_buf[sample_idx] = (int16_t)ADC12MEM1 - 2048;
        sample_idx++;
        
        if (sample_idx >= FFT_SIZE) {
            cycle_complete = 1;
            // 停止ADC触发
        }
    }
}

/* ========== 简易DFT(不用FFT库，MSP430资源有限) ========== */
// 只计算需要的频率点(基波和各次谐波)，不做完整FFT
// 计算量: 10次谐波 × N次乘加 = 10 × 256 × 2 = 5120次运算

typedef struct {
    float real;
    float imag;
    float magnitude;
    float phase;
} DFT_Bin_t;

DFT_Bin_t dft_single_bin(int16_t* data, uint16_t N, uint16_t k)
{
    DFT_Bin_t bin = {0, 0, 0, 0};
    float angle_step = 2.0f * 3.14159265f * k / N;
    
    for (uint16_t n = 0; n < N; n++) {
        float angle = angle_step * n;
        bin.real += (float)data[n] * cosf(angle);
        bin.imag -= (float)data[n] * sinf(angle);
    }
    
    bin.real /= (N / 2.0f);
    bin.imag /= (N / 2.0f);
    bin.magnitude = sqrtf(bin.real * bin.real + bin.imag * bin.imag);
    bin.phase = atan2f(bin.imag, bin.real);
    
    return bin;
}

/* ========== 功率测量主函数 ========== */
void power_measure(void)
{
    if (!cycle_complete) return;
    cycle_complete = 0;
    
    // ===== 1. 时域计算: Vrms, Irms, P =====
    float sum_v2 = 0, sum_i2 = 0, sum_vi = 0;
    
    for (uint16_t i = 0; i < SAMPLES_PER_CYC; i++) {
        float v = (float)voltage_buf[i] * V_SCALE;
        float ii = (float)current_buf[i] * I_SCALE;
        sum_v2 += v * v;
        sum_i2 += ii * ii;
        sum_vi += v * ii;
    }
    
    result.v_rms = sqrtf(sum_v2 / SAMPLES_PER_CYC);
    result.i_rms = sqrtf(sum_i2 / SAMPLES_PER_CYC);
    result.power_active = sum_vi / SAMPLES_PER_CYC;
    result.power_apparent = result.v_rms * result.i_rms;
    
    if (result.power_apparent > 0.001f) {
        result.power_factor = result.power_active / result.power_apparent;
    } else {
        result.power_factor = 0;
    }
    
    // ===== 2. 频域计算: 电流谐波 =====
    // 基波频率bin = 50Hz × FFT_SIZE / SAMPLE_RATE = 50×256/10000 ≈ 1.28
    // 取整为1（实际频率分辨率=10000/256=39.06Hz，最近的bin）
    // 更精确：用SAMPLES_PER_CYC=200点，基波在bin=1处
    
    uint16_t fund_bin = (uint16_t)(50.0f * FFT_SIZE / SAMPLE_RATE + 0.5f);
    
    float sum_harm_sq = 0;
    for (uint16_t h = 1; h <= NUM_HARMONICS; h++) {
        uint16_t bin_idx = fund_bin * h;
        if (bin_idx >= FFT_SIZE / 2) break;
        
        DFT_Bin_t bin = dft_single_bin(current_buf, FFT_SIZE, bin_idx);
        result.i_harmonic[h] = bin.magnitude * I_SCALE / 1.414f;  // 峰值→有效值
    }
    
    // ===== 3. 计算THD =====
    if (result.i_harmonic[1] > 0.001f) {
        for (uint16_t h = 2; h <= NUM_HARMONICS; h++) {
            sum_harm_sq += result.i_harmonic[h] * result.i_harmonic[h];
        }
        result.i_thd = sqrtf(sum_harm_sq) / result.i_harmonic[1] * 100.0f;
    } else {
        result.i_thd = 0;
    }
    
    // 重新启动采样
    sample_idx = 0;
}

/* ========== 获取测量结果 ========== */
PowerResult_t* get_power_result(void)
{
    return &result;
}
