/**
 * 频率检测模块 —— 实现
 * 
 * ============================================================
 * 【过零检测的改进：线性插值提高精度】
 * ============================================================
 * 
 * 普通过零检测：找到data[i-1]<0且data[i]>=0的索引i
 * 精度 = 1个采样间隔 = 1/fs
 * 
 * 改进：在过零点附近做线性插值
 * 精确的过零时刻 = i-1 + |data[i-1]| / (|data[i-1]| + |data[i]|)
 * 精度提升约10倍
 * 
 * 例：fs=100kHz, 信号1kHz
 *   普通过零：精度=10μs/1000μs=1%
 *   插值过零：精度≈1μs/1000μs=0.1%
 * 
 * 0.1%的频率精度 → 同步采样率误差0.1%
 * → FFT基频偏离bin中心0.001个bin → 频谱泄漏可忽略
 * 
 * ============================================================
 */

#include "freq_detect.h"
#include <math.h>

float FreqDetect_Measure(int16_t* data, uint16_t len, float fs)
{
    /*
     * 【噪声处理】
     * 如果信号很小（30mV），ADC噪声可能导致虚假过零。
     * 解决：加一个迟滞阈值（hysteresis）
     * 只有从<-threshold上升到>+threshold才算一次过零。
     * 
     * threshold选多大？
     * MSP432 14bit ADC的噪声约±4 LSB
     * 30mV信号的峰值约±37 LSB（低量程放大10倍后约±370 LSB）
     * threshold = 10 LSB → 远大于噪声，远小于信号 → 合适
     */
    const int16_t HYSTERESIS = 10;
    
    // 状态机：LOW → HIGH 时记录过零点
    typedef enum { STATE_LOW, STATE_HIGH } ZeroState_t;
    ZeroState_t state = (data[0] > HYSTERESIS) ? STATE_HIGH : STATE_LOW;
    
    float cross_times[128];  // 最多记录128个过零点
    int num_crosses = 0;
    
    for (uint16_t i = 1; i < len && num_crosses < 128; i++) {
        if (state == STATE_LOW && data[i] > HYSTERESIS) {
            // 上升沿过零！
            state = STATE_HIGH;
            
            // 线性插值求精确过零时刻
            // data[i-1] < 0, data[i] > 0
            // 过零点 = (i-1) + |data[i-1]| / (|data[i-1]| + data[i])
            float t_cross;
            int16_t d_prev = data[i-1];
            int16_t d_curr = data[i];
            
            if (d_curr - d_prev != 0) {
                t_cross = (float)(i - 1) + (float)(-d_prev) / (float)(d_curr - d_prev);
            } else {
                t_cross = (float)i;
            }
            
            cross_times[num_crosses++] = t_cross;
        }
        else if (state == STATE_HIGH && data[i] < -HYSTERESIS) {
            state = STATE_LOW;
        }
    }
    
    if (num_crosses < 2) return 0;  // 不够两个过零点，无法计算频率
    
    /*
     * 频率 = (过零次数-1) / (最后一个过零时刻 - 第一个过零时刻) × fs
     * 
     * 【为什么不用相邻过零间隔的平均值？】
     * 因为每个间隔的测量误差是独立的，
     * 用首尾间隔计算等价于对所有间隔取平均，
     * 但只需要做一次除法，更简单也更准确。
     */
    float total_time = (cross_times[num_crosses - 1] - cross_times[0]) / fs;
    float freq = (float)(num_crosses - 1) / total_time;
    
    return freq;
}

float FreqDetect_CalcSyncRate(float freq, float fs_init)
{
    /*
     * 目标：找到整数M，使得 fs_sync = freq × FFT_SIZE / M 最接近 fs_init
     * 
     * M = round(freq × FFT_SIZE / fs_init)
     * fs_sync = freq × FFT_SIZE / M
     * 
     * 例：freq=1kHz, FFT_SIZE=1024, fs_init=100kHz
     *   M = round(1000 × 1024 / 100000) = round(10.24) = 10
     *   fs_sync = 1000 × 1024 / 10 = 102400Hz
     *   
     * 例：freq=50kHz, FFT_SIZE=1024, fs_init=1000000
     *   M = round(50000 × 1024 / 1000000) = round(51.2) = 51
     *   fs_sync = 50000 × 1024 / 51 = 1003922Hz
     *   
     * 【边界情况】
     * 如果freq很低(100Hz)，M=round(100×1024/100000)=1
     * fs_sync = 100 × 1024 / 1 = 102400Hz → 合理
     * 
     * 如果freq很高(100kHz)，M=round(100000×1024/1000000)=102
     * fs_sync = 100000 × 1024 / 102 = 1003922Hz → 接近1MSPS极限
     * 
     * 如果fs_sync超过MCU的ADC最高采样率，需要增大M：
     * M_min = ceil(freq × FFT_SIZE / MAX_SAMPLE_RATE)
     */
    
    float M_float = freq * FFT_SIZE / fs_init;
    int M = (int)(M_float + 0.5f);
    if (M < 1) M = 1;
    
    // 检查是否超过最高采样率
    float fs_sync = freq * FFT_SIZE / M;
    while (fs_sync > MAX_SAMPLE_RATE && M < FFT_SIZE) {
        M++;
        fs_sync = freq * FFT_SIZE / M;
    }
    
    // 检查是否低于最低合理采样率（至少10倍过采样）
    float min_rate = freq * 10.0f;
    while (fs_sync < min_rate && M > 1) {
        M--;
        fs_sync = freq * FFT_SIZE / M;
    }
    
    return fs_sync;
}
