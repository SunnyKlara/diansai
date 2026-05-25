/**
 * @file    thd_measure.h
 * @brief   THD（总谐波失真）测量算法接口
 *
 *  核心算法：
 *      ADC raw → 加平顶窗 → FFT → 幅度谱 → 提取基波 + 5 次谐波 → 计算 THD
 *
 *  设计原则：
 *      - 与硬件解耦（输入 ADC raw 数组 + 转换系数，输出结果结构）
 *      - 不依赖 HAL 库
 *      - 依赖 CMSIS-DSP 的 arm_cfft_f32 / arm_cmplx_mag_f32
 *
 *  精度策略：
 *      - 同步采样消除频谱泄漏（采样率由 freq_detect 计算）
 *      - 平顶窗双保险（即使同步不完美也能保证幅度精度 < 0.01dB）
 *      - 在峰值附近 ±1 取最大值，补偿微小频率偏移
 */

#ifndef __THD_MEASURE_H
#define __THD_MEASURE_H

#include <stdint.h>

typedef struct {
    float thd_percent;          /* THD (%) */
    float fundamental_vrms;     /* 基波有效值 (V) */
    float harmonic_vrms[6];     /* harmonic_vrms[1] = 基波，[2]~[5] = 谐波 */
    float harmonic_norm[6];     /* 归一化（除以基波）：[1] = 1.0，[2]~[5] = 谐波/基波 */
    float frequency;            /* 测量到的基波频率 (Hz) */
} THDResult_t;

/**
 * @brief 初始化 FFT 实例
 *  必须在第一次 measure_thd 之前调用一次。
 */
void thd_init(void);

/**
 * @brief 初始化平顶窗系数表
 *  必须在 thd_init 之后、measure_thd 之前调用一次。
 */
void init_flat_top_window(void);

/**
 * @brief 完整 THD 测量
 * @param adc_data  原始 ADC 数据（uint16_t，0 ~ 16383）
 * @param len       数据长度（必须等于 FFT_SIZE）
 * @param fs        实际采样率（Hz）
 * @param v_scale   ADC 值到电压的换算系数（V/LSB）
 * @param v_offset  ADC 零点偏置（LSB）
 * @return 测量结果
 */
THDResult_t measure_thd(uint16_t *adc_data, uint16_t len, float fs,
                        float v_scale, float v_offset);

/**
 * @brief 粗测频率（过零检测，工具函数，单元测试用）
 *  freq_detect 模块也实现了一份；此处保留给 main 使用一致接口。
 */
float coarse_freq_measure(int16_t *data, uint16_t len, float fs);

/**
 * @brief 同步采样率计算（工具函数）
 */
float calc_sync_sample_rate(float freq, float fs_init);

#endif /* __THD_MEASURE_H */
