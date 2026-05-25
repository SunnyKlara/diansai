/**
 * 信号处理模块 —— 头文件
 * 
 * 推理阶段的核心：
 * 输入信号 → FFT → 对每个频率分量应用频率响应 → IFFT → 输出信号
 */

#ifndef __SIGNAL_PROCESSOR_H
#define __SIGNAL_PROCESSOR_H

#include "config.h"

/**
 * 初始化信号处理模块（分配FFT实例等）
 */
void SigProc_Init(void);

/**
 * 处理一帧信号
 * 
 * @param input   输入信号（ADC采样值，int16_t，去偏置后）
 * @param output  输出信号（DAC值，uint16_t，含偏置）
 * @param length  帧长度（必须等于FFT_SIZE）
 * @param fs      采样率(Hz)
 * 
 * 处理流程：
 * 1. int16_t → float，归一化
 * 2. FFT
 * 3. 对每个频率bin查频率响应表，应用增益和相位
 * 4. IFFT
 * 5. float → uint16_t（加偏置2048，限幅0~4095）
 */
void SigProc_ProcessFrame(int16_t* input, uint16_t* output, uint16_t length, float fs);

#endif
