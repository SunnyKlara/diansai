/**
 * 频率响应测量与存储模块 —— 头文件
 *
 * 学习阶段：扫频测量未知电路的频率响应，存储为查找表供推理阶段使用。
 *
 * 架构约束（Steering 强制）：
 *   - 算法层不依赖 HAL / driverlib / CMSIS-DSP
 *   - 硬件相关动作（DDS 设频、ADC 采样、延时）通过回调注入
 *   - 数学计算（DFT、相位解缠绕、识别）纯 C 实现，可 PC 端验证
 */

#ifndef __FREQ_RESPONSE_H
#define __FREQ_RESPONSE_H

#include "../config.h"

/* ========================================================================
 * 数据类型
 * ===================================================================== */

typedef struct {
    float frequency;        /* 频率（Hz）          */
    float gain;             /* 增益（线性，非 dB） */
    float phase_rad;        /* 相位（rad，已解缠绕）*/
} FreqPoint_t;

typedef enum {
    FILT_LOWPASS,
    FILT_HIGHPASS,
    FILT_BANDPASS,
    FILT_BANDSTOP,
    FILT_UNKNOWN
} FilterType_t;

/* ========================================================================
 * 回调钩子（由调用方在 Core/main.c 实现）
 * ===================================================================== */

/**
 * @brief 设置 DDS 输出频率并启动正弦波。
 *        由 Core 实现，内部调用 Drivers/dds_ad9833.c。
 */
typedef void (*FreqResp_DDSSetFn)(float freq_hz);

/**
 * @brief 阻塞延时（ms），由 Core 实现（HAL_Delay / OSDelay）。
 */
typedef void (*FreqResp_DelayFn)(uint32_t ms);

/**
 * @brief 同步采集 N 点输入/输出 IQ 数据。
 * @param  freq_hz       目标频率（用于决定采样率）
 * @param[out] in_data   ADC1（输入端）数据指针
 * @param[out] out_data  ADC2（输出端）数据指针
 * @param[out] sample_rate 实际采样率（Hz）
 * @param[out] length    实际采样点数
 * @return 1 成功 / 0 失败
 *
 * 由 Core 实现，内部调用 Drivers/adc_dual_sync.c。
 */
typedef uint8_t (*FreqResp_CaptureFn)(float freq_hz,
                                      const int16_t **in_data,
                                      const int16_t **out_data,
                                      float *sample_rate,
                                      uint16_t *length);

typedef struct {
    FreqResp_DDSSetFn   dds_set_freq;
    FreqResp_DelayFn    delay_ms;
    FreqResp_CaptureFn  capture;
} FreqResp_Callbacks_t;

/* ========================================================================
 * API
 * ===================================================================== */

/**
 * @brief 注入硬件回调（必须在 Learn 前调用）
 */
void FreqResp_BindCallbacks(const FreqResp_Callbacks_t *cb);

/**
 * @brief 执行完整扫频学习（粗扫 + 类型识别 + 细扫 + 解缠绕）
 * @return 测量的频率点数
 */
uint16_t FreqResp_Learn(void);

/**
 * @brief 单频测量（暴露给单元测试，PC 端可注入虚假回调）
 *        计算流程：DFT 单频提取增益 + 相位
 */
void FreqResp_MeasureOnePoint(const int16_t *in, const int16_t *out,
                              uint16_t length, float sample_rate,
                              float freq_hz, float *gain, float *phase);

/**
 * @brief 获取识别的滤波器类型
 */
FilterType_t FreqResp_GetFilterType(void);

/**
 * @brief 在频率响应表中线性插值查询
 */
void FreqResp_Interpolate(float freq_hz, float *gain, float *phase);

/**
 * @brief 获取扫频结果表（用于显示或调试）
 */
FreqPoint_t *FreqResp_GetTable(uint16_t *length);

#endif
