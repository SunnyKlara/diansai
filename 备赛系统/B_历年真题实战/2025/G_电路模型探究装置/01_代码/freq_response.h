/**
 * 频率响应测量与存储模块 —— 头文件
 * 
 * 这是学习阶段的核心：扫频测量未知电路的频率响应，
 * 存储为查找表供推理阶段使用。
 */

#ifndef __FREQ_RESPONSE_H
#define __FREQ_RESPONSE_H

#include "config.h"

/* 频率响应数据点 */
typedef struct {
    float frequency;    // 频率 (Hz)
    float gain;         // 增益 (线性值，不是dB)
    float phase_rad;    // 相位 (rad，已解缠绕)
} FreqPoint_t;

/* 滤波器类型 */
typedef enum {
    FILT_LOWPASS,
    FILT_HIGHPASS,
    FILT_BANDPASS,
    FILT_BANDSTOP,
    FILT_UNKNOWN
} FilterType_t;

/**
 * 执行完整的扫频学习过程
 * 包括：粗扫→判断类型→细扫→存储
 * @return 测量的频率点数
 */
uint16_t FreqResp_Learn(void);

/**
 * 获取识别的滤波器类型
 */
FilterType_t FreqResp_GetFilterType(void);

/**
 * 在频率响应表中插值查询
 * @param freq_hz  查询频率(Hz)
 * @param gain     输出增益(线性)
 * @param phase    输出相位(rad)
 */
void FreqResp_Interpolate(float freq_hz, float* gain, float* phase);

/**
 * 获取频率响应数据（用于显示或调试）
 */
FreqPoint_t* FreqResp_GetTable(uint16_t* length);

#endif
