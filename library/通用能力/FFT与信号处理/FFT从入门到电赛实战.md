# FFT —— 从入门到电赛实战

> FFT是电赛仪表类、电源类、通信类题目的核心算法

## 一、为什么需要FFT

把时域信号变换到频域，看到每个频率分量的幅度和相位。

**电赛中的典型应用：**
- 测量THD（总谐波失真）
- 测量信号频率
- 谐波分析（各次谐波幅值）
- 调制方式识别
- 信号分离

## 二、STM32上的FFT实现

### 方案：使用CMSIS-DSP库（推荐）

```c
#include "arm_math.h"

#define FFT_SIZE 1024

// 输入输出缓冲区
float32_t fft_input[FFT_SIZE * 2];   // 复数输入（实部+虚部交替）
float32_t fft_output[FFT_SIZE];       // 幅度谱

arm_cfft_instance_f32 fft_instance;

void fft_init(void) {
    arm_cfft_init_f32(&fft_instance, FFT_SIZE);
}

// 对ADC采样数据做FFT
void do_fft(uint16_t* adc_data, uint16_t len) {
    // 1. 填充输入缓冲区（实部=采样值，虚部=0）
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input[2*i] = (float32_t)adc_data[i] / 4096.0f * 3.3f;  // 实部
        fft_input[2*i + 1] = 0;  // 虚部
    }
    
    // 2. 执行FFT
    arm_cfft_f32(&fft_instance, fft_input, 0, 1);  // 0=正变换, 1=位反转
    
    // 3. 计算幅度谱
    arm_cmplx_mag_f32(fft_input, fft_output, FFT_SIZE);
    
    // 4. 归一化（除以N/2，直流分量除以N）
    fft_output[0] /= FFT_SIZE;
    for (int i = 1; i < FFT_SIZE/2; i++) {
        fft_output[i] /= (FFT_SIZE / 2);
    }
}

// 找基波频率
float find_fundamental_freq(float sample_rate) {
    uint32_t max_index = 0;
    float32_t max_value = 0;
    
    // 跳过直流分量（index=0），从index=1开始找最大值
    for (int i = 1; i < FFT_SIZE/2; i++) {
        if (fft_output[i] > max_value) {
            max_value = fft_output[i];
            max_index = i;
        }
    }
    
    // 频率 = index × 频率分辨率
    float freq_resolution = sample_rate / FFT_SIZE;
    return max_index * freq_resolution;
}

// 计算THD
float calc_thd(uint32_t fundamental_index) {
    float v1 = fft_output[fundamental_index];  // 基波幅值
    float sum_sq = 0;
    
    // 2~5次谐波
    for (int h = 2; h <= 5; h++) {
        uint32_t idx = fundamental_index * h;
        if (idx < FFT_SIZE/2) {
            sum_sq += fft_output[idx] * fft_output[idx];
        }
    }
    
    return sqrtf(sum_sq) / v1 * 100.0f;  // 百分比
}
```

## 三、关键参数选择

### 采样率 fs
- 必须 ≥ 2 × 最高频率（奈奎斯特定理）
- 实际取 5~10 倍最高频率
- 例：测量10次谐波(500Hz)，fs ≥ 5kHz，推荐10kHz

### FFT点数 N
- 频率分辨率 = fs / N
- 例：fs=10kHz, N=1024 → 分辨率=9.77Hz
- 点数越多精度越高，但计算时间越长
- 电赛常用：256、512、1024、2048

### 窗函数
- 不加窗：频谱泄漏严重
- 汉宁窗：通用选择，幅度精度好
- 平顶窗：幅度精度最好（测幅值用）
- 矩形窗：频率分辨率最好（测频率用）

```c
// 汉宁窗
void apply_hanning(float* data, int N) {
    for (int i = 0; i < N; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (N - 1)));
        data[2*i] *= w;  // 只对实部加窗
    }
}
```

## 四、同步采样（重要！）

### 问题
如果采样窗口不包含整数个信号周期，FFT结果会有频谱泄漏，幅度测量不准。

### 解决方案
1. **硬件同步**：用信号的过零点触发ADC采样
2. **软件补偿**：先粗测频率，调整采样率使N个点恰好覆盖整数个周期
3. **加窗函数**：减轻泄漏影响（但不能完全消除）

```c
// 软件同步采样
void sync_sample(float target_freq) {
    // 计算需要的采样率，使得N个点恰好覆盖M个周期
    int M = (int)(target_freq * FFT_SIZE / INITIAL_SAMPLE_RATE + 0.5);
    float adjusted_fs = target_freq * FFT_SIZE / M;
    
    // 重新配置定时器的采样率
    set_adc_sample_rate(adjusted_fs);
}
```

## 五、电赛各题目中FFT的具体用法

| 题目 | FFT用途 | 采样率 | 点数 | 注意事项 |
|------|---------|--------|------|----------|
| 2021A THD测量 | 提取谐波幅值算THD | ≥1MSPS | 1024+ | 需要同步采样 |
| 2024B 功率分析 | 电流谐波分析 | 10~50kHz | 1024 | 10次谐波 |
| 2025B APF | 检测谐波电流 | 10~50kHz | 512 | 实时性要求高 |
| 2023D 调制识别 | 频谱特征分析 | ≥4MSPS | 2048 | 载波2MHz |
| 2025G 电路建模 | 频率响应测量 | 可变 | 1024 | 扫频+FFT |
