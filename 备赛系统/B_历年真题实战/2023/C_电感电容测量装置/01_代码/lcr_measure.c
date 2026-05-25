/**
 * 2023C 电感电容测量 - I-V法核心算法
 * MCU: TI MSP432P401R
 * 
 * 原理：
 *   DDS产生正弦激励信号 → 串联已知电阻R0 → 被测元件Zx → GND
 *   同时采样R0两端电压Vr和Zx两端电压Vx
 *   通过DFT提取幅值和相位 → 计算Zx → 得到L/C/Q/D
 * 
 * 测量范围：
 *   电容: 1nF ~ 100nF, D值: 0.005 ~ 1
 *   电感: 10μH ~ 100μH, Q值: 1 ~ 200
 */

#include <msp430.h>
#include <math.h>
#include <stdint.h>

#define PI 3.14159265f
#define SAMPLE_POINTS 200   // 每周期采样点数
#define R0 1000.0f          // 已知串联电阻 1kΩ

/* ========== DFT单频提取 ========== */
typedef struct {
    float amplitude;
    float phase;
} SineFit_t;

/**
 * 对采样数据做单频DFT，提取基波幅值和相位
 * @param data   ADC采样数据(去偏置后)
 * @param N      采样点数
 * @return 幅值和相位
 */
SineFit_t extract_fundamental(int16_t* data, uint16_t N)
{
    SineFit_t result;
    float sum_sin = 0, sum_cos = 0;
    
    for (uint16_t i = 0; i < N; i++) {
        float angle = 2.0f * PI * i / N;
        sum_sin += (float)data[i] * sinf(angle);
        sum_cos += (float)data[i] * cosf(angle);
    }
    
    sum_sin *= 2.0f / N;
    sum_cos *= 2.0f / N;
    
    result.amplitude = sqrtf(sum_sin * sum_sin + sum_cos * sum_cos);
    result.phase = atan2f(sum_sin, sum_cos);
    
    return result;
}

/* ========== 阻抗计算 ========== */
typedef struct {
    float impedance_mag;   // |Zx| (Ω)
    float impedance_phase; // ∠Zx (rad)
    float resistance;      // Re(Zx) (Ω)
    float reactance;       // Im(Zx) (Ω)
} Impedance_t;

/**
 * 根据Vr和Vx计算被测阻抗
 * Zx = R0 × Vx / Vr × exp(j(φx - φr))
 * 
 * 简化：Zx = R0 × (Vx_amp/Vr_amp) × exp(j×Δφ)
 */
Impedance_t calc_impedance(SineFit_t vr, SineFit_t vx)
{
    Impedance_t z;
    
    float ratio = vx.amplitude / vr.amplitude;
    float delta_phase = vx.phase - vr.phase;
    
    z.impedance_mag = R0 * ratio;
    z.impedance_phase = delta_phase;
    z.resistance = z.impedance_mag * cosf(delta_phase);
    z.reactance = z.impedance_mag * sinf(delta_phase);
    
    return z;
}

/* ========== 元件参数计算 ========== */
typedef struct {
    float capacitance;  // 电容值 (F)
    float d_value;      // 损耗角正切 D
    float inductance;   // 电感值 (H)
    float q_value;      // 品质因数 Q
    uint8_t type;       // 0=电容, 1=电感
} ComponentResult_t;

/**
 * 根据阻抗计算元件参数
 * @param z     阻抗测量结果
 * @param freq  测量频率 (Hz)
 */
ComponentResult_t calc_component(Impedance_t z, float freq)
{
    ComponentResult_t result = {0};
    float omega = 2.0f * PI * freq;
    
    if (z.reactance < 0) {
        // 容性（虚部为负）
        result.type = 0;
        result.capacitance = -1.0f / (omega * z.reactance);
        if (z.reactance != 0) {
            result.d_value = z.resistance / (-z.reactance);
        }
    } else {
        // 感性（虚部为正）
        result.type = 1;
        result.inductance = z.reactance / omega;
        if (z.resistance > 0.001f) {
            result.q_value = z.reactance / z.resistance;
        }
    }
    
    return result;
}

/* ========== 采样缓冲区 ========== */
static int16_t vr_buf[SAMPLE_POINTS];  // R0两端电压
static int16_t vx_buf[SAMPLE_POINTS];  // Zx两端电压

/* ========== 完整测量流程 ========== */

// ADC缩放系数 (根据实际电路调整)
#define ADC_SCALE (3.3f / 4096.0f)  // 12bit ADC, 3.3V参考

ComponentResult_t measure_component(float test_freq)
{
    // 1. 设置DDS输出频率
    // DDS_SetFreq(test_freq);  // 需要实现DDS驱动
    // HAL_Delay(100);  // 等待稳定
    
    // 2. 同步采样Vr和Vx
    // 采样率 = test_freq × SAMPLE_POINTS
    // 例：test_freq=10kHz → 采样率=2MHz
    // 需要配置ADC和Timer
    
    // 3. 去除直流偏置
    int32_t vr_sum = 0, vx_sum = 0;
    for (int i = 0; i < SAMPLE_POINTS; i++) {
        vr_sum += vr_buf[i];
        vx_sum += vx_buf[i];
    }
    int16_t vr_dc = vr_sum / SAMPLE_POINTS;
    int16_t vx_dc = vx_sum / SAMPLE_POINTS;
    for (int i = 0; i < SAMPLE_POINTS; i++) {
        vr_buf[i] -= vr_dc;
        vx_buf[i] -= vx_dc;
    }
    
    // 4. DFT提取基波
    SineFit_t vr_fit = extract_fundamental(vr_buf, SAMPLE_POINTS);
    SineFit_t vx_fit = extract_fundamental(vx_buf, SAMPLE_POINTS);
    
    // 5. 转换为实际电压
    vr_fit.amplitude *= ADC_SCALE;
    vx_fit.amplitude *= ADC_SCALE;
    
    // 6. 计算阻抗
    Impedance_t z = calc_impedance(vr_fit, vx_fit);
    
    // 7. 计算元件参数
    ComponentResult_t result = calc_component(z, test_freq);
    
    return result;
}
