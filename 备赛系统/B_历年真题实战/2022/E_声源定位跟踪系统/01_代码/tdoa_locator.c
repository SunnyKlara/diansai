/**
 * 2022E 声源TDOA定位核心算法
 * 
 * ============================================================
 * 【为什么大多数队伍的声源定位精度不够？】
 * ============================================================
 * 
 * 常见错误1：用普通互相关求时延
 *   普通互相关在有回声和噪声的环境下，相关峰会被污染。
 *   室内环境回声严重（墙壁、桌面反射），这是最大的干扰源。
 *   → 解决：用GCC-PHAT（广义互相关-相位变换），它对回声有天然的抑制能力。
 * 
 * 常见错误2：用3个麦克风做2组TDOA，解双曲线交点
 *   双曲线交点的解析解在某些几何配置下不稳定（病态问题）。
 *   特别是声源在麦克风阵列的延长线方向时，定位误差会爆炸。
 *   → 解决：用4个麦克风做3组TDOA，用最小二乘法过约束求解，提高鲁棒性。
 * 
 * 常见错误3：忽略声速的温度依赖
 *   声速 v = 331.3 + 0.606 × T(℃)
 *   20℃时v=343.5m/s，30℃时v=349.5m/s，差6m/s
 *   对于3m距离，这导致约5cm的定位误差
 *   → 解决：赛前用已知距离标定声速，或用温度传感器实时补偿。
 * 
 * 常见错误4：采样率不够高
 *   44.1kHz采样率，一个采样间隔对应的距离 = 343/44100 = 7.8mm
 *   这是时延估计的最小分辨率。
 *   但互相关的峰值位置可以通过抛物线插值达到亚采样精度！
 *   → 解决：在互相关峰值附近做3点抛物线插值，精度提升5~10倍。
 * 
 * ============================================================
 */

#include "arm_math.h"
#include <math.h>
#include <string.h>

#define SAMPLE_RATE     48000    // 48kHz采样率（比44.1kHz好算）
#define FRAME_SIZE      2048     // 每帧采样点数（约42ms）
#define NUM_MICS        4        // 4个麦克风（过约束提高精度）
#define SOUND_SPEED     343.5f   // 声速(m/s)，需要标定！

/* ========== GCC-PHAT互相关 ========== */

/**
 * GCC-PHAT（广义互相关-相位变换）
 * 
 * 与普通互相关的区别：
 *   普通互相关：R(τ) = IFFT(X × conj(Y))
 *   GCC-PHAT：  R(τ) = IFFT(X × conj(Y) / |X × conj(Y)|)
 *   
 * PHAT加权的效果：将互功率谱的幅度归一化为1，只保留相位信息。
 * 这使得相关峰更尖锐，对回声和噪声的抑制能力大幅提升。
 * 
 * 代价：信噪比很低时（<0dB），PHAT会放大噪声。
 * 但电赛场景下声源距离<3m，信噪比通常>10dB，PHAT是最优选择。
 */

static float32_t fft_buf_a[FRAME_SIZE * 2];  // 复数FFT缓冲区
static float32_t fft_buf_b[FRAME_SIZE * 2];
static float32_t gcc_result[FRAME_SIZE * 2];
static float32_t gcc_mag[FRAME_SIZE];

static arm_cfft_instance_f32 fft_inst;

void tdoa_init(void)
{
    arm_cfft_init_f32(&fft_inst, FRAME_SIZE);
}

/**
 * 计算两路信号的TDOA（到达时间差）
 * 
 * @param sig_a  麦克风A的采样数据（int16_t, 去偏置后）
 * @param sig_b  麦克风B的采样数据
 * @param len    数据长度（=FRAME_SIZE）
 * @return 时延（秒），正值表示A先到达，负值表示B先到达
 */
float calc_tdoa(int16_t* sig_a, int16_t* sig_b, uint16_t len)
{
    // 1. 填充FFT缓冲区（实部=信号，虚部=0）
    for (int i = 0; i < len; i++) {
        fft_buf_a[2*i]     = (float32_t)sig_a[i] / 32768.0f;
        fft_buf_a[2*i + 1] = 0;
        fft_buf_b[2*i]     = (float32_t)sig_b[i] / 32768.0f;
        fft_buf_b[2*i + 1] = 0;
    }
    
    // 2. FFT
    arm_cfft_f32(&fft_inst, fft_buf_a, 0, 1);
    arm_cfft_f32(&fft_inst, fft_buf_b, 0, 1);
    
    // 3. 互功率谱 + PHAT加权
    for (int i = 0; i < len; i++) {
        float ar = fft_buf_a[2*i], ai = fft_buf_a[2*i+1];
        float br = fft_buf_b[2*i], bi = fft_buf_b[2*i+1];
        
        // X × conj(Y)
        float cross_r = ar * br + ai * bi;
        float cross_i = ai * br - ar * bi;
        
        // PHAT加权：除以幅度
        float mag = sqrtf(cross_r * cross_r + cross_i * cross_i);
        if (mag > 1e-10f) {
            gcc_result[2*i]     = cross_r / mag;
            gcc_result[2*i + 1] = cross_i / mag;
        } else {
            gcc_result[2*i]     = 0;
            gcc_result[2*i + 1] = 0;
        }
    }
    
    // 4. IFFT得到互相关函数
    arm_cfft_f32(&fft_inst, gcc_result, 1, 1);  // 逆变换
    
    // 5. 取实部的幅度
    for (int i = 0; i < len; i++) {
        gcc_mag[i] = gcc_result[2*i];  // 实部就是互相关值
    }
    
    // 6. 找峰值
    // 搜索范围：±max_delay
    // max_delay = 最大麦克风间距 / 声速 × 采样率
    // 假设最大间距0.5m → max_delay = 0.5/343.5*48000 ≈ 70个采样点
    int max_delay_samples = 80;
    
    float max_val = -1e10f;
    int max_idx = 0;
    
    // 互相关结果是循环的：正延迟在前半部分，负延迟在后半部分
    for (int i = 0; i < max_delay_samples; i++) {
        if (gcc_mag[i] > max_val) {
            max_val = gcc_mag[i];
            max_idx = i;
        }
    }
    for (int i = len - max_delay_samples; i < len; i++) {
        if (gcc_mag[i] > max_val) {
            max_val = gcc_mag[i];
            max_idx = i;
        }
    }
    
    // 将循环索引转换为有符号延迟
    int delay_samples = max_idx;
    if (delay_samples > len / 2) {
        delay_samples -= len;  // 负延迟
    }
    
    // 7. 【关键】抛物线插值提高精度
    //    在峰值附近3个点做抛物线拟合，找到亚采样精度的峰值位置
    //    这一步能将精度从7.8mm提升到约1mm！
    float y_prev, y_peak, y_next;
    int peak_idx = max_idx;
    
    y_peak = gcc_mag[peak_idx];
    y_prev = gcc_mag[(peak_idx - 1 + len) % len];
    y_next = gcc_mag[(peak_idx + 1) % len];
    
    // 抛物线插值公式：δ = (y_prev - y_next) / (2 × (y_prev - 2×y_peak + y_next))
    float denom = 2.0f * (y_prev - 2.0f * y_peak + y_next);
    float delta = 0;
    if (fabsf(denom) > 1e-10f) {
        delta = (y_prev - y_next) / denom;
    }
    
    float refined_delay = (float)delay_samples + delta;
    
    // 8. 转换为时间（秒）
    return refined_delay / (float)SAMPLE_RATE;
}

/* ========== 位置解算：过约束最小二乘法 ========== */

/**
 * 【为什么用4个麦克风而不是3个？】
 * 
 * 3个麦克风 → 2组TDOA → 2个方程2个未知数 → 恰好约束
 * 问题：如果某一组TDOA有误差，没有冗余信息来纠正
 * 
 * 4个麦克风 → 3组TDOA → 3个方程2个未知数 → 过约束
 * 用最小二乘法求解，误差被平均化，精度更高
 * 
 * 麦克风布局建议：
 *   mic0(0, 0)        ← 参考麦克风
 *   mic1(d, 0)        ← X轴方向
 *   mic2(0, d)        ← Y轴方向
 *   mic3(d, d)        ← 对角方向
 *   d ≈ 30cm
 *   
 * 这种布局在所有方向上都有良好的分辨率。
 * 避免使用线阵（线阵在垂直于阵列方向的分辨率很差）。
 */

typedef struct {
    float x, y;  // 位置(mm)
} Point2D_t;

// 麦克风位置（需要精确测量！误差直接影响定位精度）
static Point2D_t mic_pos[NUM_MICS] = {
    {0, 0},       // mic0: 参考点
    {300, 0},     // mic1: X方向30cm
    {0, 300},     // mic2: Y方向30cm
    {300, 300}    // mic3: 对角30cm
};

/**
 * 声源定位
 * 
 * @param tdoa  TDOA数组，tdoa[i] = mic0到达时间 - mic_i到达时间（秒）
 *              正值表示声源离mic_i更近
 * @return 声源位置(mm)
 * 
 * 算法：线性化最小二乘法
 * 
 * 原始方程（非线性）：
 *   |r - mic_i| - |r - mic_0| = v × tdoa[i]
 *   
 * 线性化（两两相减消去|r-mic_0|²项）：
 *   2(mic_i - mic_j) · r = (|mic_i|² - |mic_j|²) - (di² - dj²)
 *   其中 di = v × tdoa[i]
 *   
 * 写成矩阵形式 A·r = b，用最小二乘法求解 r = (A^T·A)^(-1) · A^T · b
 */
Point2D_t locate_source(float tdoa[NUM_MICS])
{
    // 距离差（相对于mic0）
    float d[NUM_MICS];
    for (int i = 0; i < NUM_MICS; i++) {
        d[i] = SOUND_SPEED * tdoa[i] * 1000.0f;  // 转换为mm
    }
    
    // 构建线性方程组 A·[x,y]^T = b
    // 用mic1,mic2,mic3分别与mic0配对，得到3个方程
    // 方程i: 2(xi-x0)·x + 2(yi-y0)·y = (xi²+yi²) - (x0²+y0²) - (di²-d0²)
    // 其中d0=0（mic0是参考）
    
    float A[3][2], b[3];
    
    for (int i = 0; i < 3; i++) {
        int mi = i + 1;  // mic1, mic2, mic3
        A[i][0] = 2.0f * (mic_pos[mi].x - mic_pos[0].x);
        A[i][1] = 2.0f * (mic_pos[mi].y - mic_pos[0].y);
        
        float mic_sq = mic_pos[mi].x * mic_pos[mi].x + mic_pos[mi].y * mic_pos[mi].y;
        float mic0_sq = mic_pos[0].x * mic_pos[0].x + mic_pos[0].y * mic_pos[0].y;
        
        b[i] = mic_sq - mic0_sq - (d[mi] * d[mi] - d[0] * d[0]);
    }
    
    // 最小二乘法求解: r = (A^T·A)^(-1) · A^T · b
    // 对于2×2矩阵，直接手算逆矩阵
    
    // A^T · A (2×2)
    float ATA[2][2] = {{0}};
    for (int i = 0; i < 3; i++) {
        ATA[0][0] += A[i][0] * A[i][0];
        ATA[0][1] += A[i][0] * A[i][1];
        ATA[1][0] += A[i][1] * A[i][0];
        ATA[1][1] += A[i][1] * A[i][1];
    }
    
    // A^T · b (2×1)
    float ATb[2] = {0};
    for (int i = 0; i < 3; i++) {
        ATb[0] += A[i][0] * b[i];
        ATb[1] += A[i][1] * b[i];
    }
    
    // 2×2矩阵求逆
    float det = ATA[0][0] * ATA[1][1] - ATA[0][1] * ATA[1][0];
    
    Point2D_t result = {0, 0};
    if (fabsf(det) > 1e-6f) {
        result.x = (ATA[1][1] * ATb[0] - ATA[0][1] * ATb[1]) / det;
        result.y = (ATA[0][0] * ATb[1] - ATA[1][0] * ATb[0]) / det;
    }
    
    return result;
}

/* ========== 完整定位流程 ========== */

// 4路麦克风采样缓冲区
static int16_t mic_buf[NUM_MICS][FRAME_SIZE];

/**
 * 执行一次完整的声源定位
 * @return 声源位置(mm)，相对于mic0
 */
Point2D_t do_localization(void)
{
    // 1. 计算3组TDOA（都以mic0为参考）
    float tdoa[NUM_MICS];
    tdoa[0] = 0;  // 参考麦克风
    
    for (int i = 1; i < NUM_MICS; i++) {
        tdoa[i] = calc_tdoa(mic_buf[0], mic_buf[i], FRAME_SIZE);
    }
    
    // 2. 位置解算
    Point2D_t pos = locate_source(tdoa);
    
    return pos;
}

/**
 * 将位置转换为极坐标（题目要求显示距离γ和角度θ）
 */
void cart_to_polar(Point2D_t pos, float* gamma_mm, float* theta_deg)
{
    *gamma_mm = sqrtf(pos.x * pos.x + pos.y * pos.y);
    *theta_deg = atan2f(pos.y, pos.x) * 180.0f / 3.14159265f;
}
