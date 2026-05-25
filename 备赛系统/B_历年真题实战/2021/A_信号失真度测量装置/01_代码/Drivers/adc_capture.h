/**
 * ADC采样模块 —— 头文件
 * 
 * 【这个模块要解决的核心问题】
 * 
 * 1. 采样率可调：粗测频率用100kHz，精确测量用同步采样率
 * 2. DMA传输：CPU不参与每个采样点的搬运，采集期间CPU可以做其他事
 * 3. 采集指定数量的点后自动停止：不是连续采集，而是"采N个点就停"
 * 
 * 【MSP432 ADC14的特殊之处】
 * 
 * 和STM32不同，MSP432的ADC14：
 * - 没有"外部定时器触发"模式（STM32有TIM TRGO触发ADC）
 * - 采样率通过ADC自身的采样定时器控制
 * - DMA通过ADC14的转换完成中断触发
 * 
 * 所以采样率的设置方式不同：
 * STM32：设置TIM的ARR → 采样率 = TIM_CLK / ARR
 * MSP432：设置ADC14的采样时间 + 使用Timer_A产生采样触发
 */

#ifndef __ADC_CAPTURE_H
#define __ADC_CAPTURE_H

#include "../config.h"

/**
 * 初始化ADC和DMA
 */
void ADC_Capture_Init(void);

/**
 * 设置采样率
 * @param sample_rate_hz  目标采样率(Hz)
 * @return 实际采样率(Hz)（可能因整数分频有微小偏差）
 * 
 * 【实现方法】
 * 用Timer_A产生周期性触发信号，每个触发启动一次ADC转换。
 * Timer_A时钟 = SMCLK = 24MHz
 * 采样率 = 24MHz / Timer_A周期
 */
float ADC_Capture_SetRate(float sample_rate_hz);

/**
 * 启动一次采集
 * @param num_samples  采集点数（≤FFT_SIZE）
 * 
 * 采集完成后自动停止，通过ADC_Capture_IsComplete()查询。
 */
void ADC_Capture_Start(uint16_t num_samples);

/**
 * 查询采集是否完成
 */
uint8_t ADC_Capture_IsComplete(void);

/**
 * 获取采集数据
 * @param data    数据指针（输出），uint16_t数组，原始ADC值(0~16383)
 * @param length  数据长度（输出）
 */
void ADC_Capture_GetData(uint16_t** data, uint16_t* length);

/**
 * 获取采集数据（有符号版本，已去偏置）
 * @param data    数据指针（输出），int16_t数组，范围约-8192~+8191
 * @param length  数据长度（输出）
 */
void ADC_Capture_GetSignedData(int16_t** data, uint16_t* length);

#endif
