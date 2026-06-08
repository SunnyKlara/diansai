/**
 * ADC双通道同步采样模块 —— 头文件
 * 
 * 【为什么同步采样是这道题的命脉？】
 * 
 * 频率响应 = Vout / Vin，包含增益和相位两个信息。
 * 增益 = |Vout| / |Vin|，对采样时间偏移不敏感。
 * 相位 = ∠Vout - ∠Vin，对采样时间偏移极其敏感！
 * 
 * 如果两个通道有Δt的采样偏移：
 *   相位误差 = 2π × f × Δt
 *   f=50kHz, Δt=1μs → 相位误差 = 18°
 *   这会导致矩形波重建完全错误。
 * 
 * STM32F407的双ADC同步模式：
 *   ADC1和ADC2由同一个定时器事件同时触发
 *   两个ADC的采样保持电路同时动作
 *   采样偏移 < 1个ADC时钟周期 ≈ 30ns
 *   f=50kHz, Δt=30ns → 相位误差 = 0.5° → 可以接受
 */

#ifndef __ADC_DUAL_SYNC_H
#define __ADC_DUAL_SYNC_H

#include "stm32f4xx_hal.h"
#include "../config.h"

/**
 * 初始化双ADC同步采样
 * 配置ADC1(PA0)和ADC2(PA1)为同步模式
 * 由TIM2 TRGO触发
 */
void ADC_DualSync_Init(void);

/**
 * 设置采样率
 * @param sample_rate_hz  采样率(Hz)
 * 
 * 通过调整TIM2的ARR值实现。
 * 实际采样率可能与设定值有微小偏差（整数分频）。
 * 返回实际采样率供后续计算使用。
 */
float ADC_DualSync_SetRate(float sample_rate_hz);

/**
 * 启动一次采集（采集指定数量的点后自动停止）
 * @param num_samples  采样点数
 * 
 * 采集完成后通过回调函数通知。
 * 数据存储在内部双缓冲区中。
 */
void ADC_DualSync_StartCapture(uint16_t num_samples);

/**
 * 检查采集是否完成
 * @return 1=完成, 0=进行中
 */
uint8_t ADC_DualSync_IsComplete(void);

/**
 * 获取采集数据指针
 * @param ch1_data  ADC1数据指针（输出）
 * @param ch2_data  ADC2数据指针（输出）
 * @param length    数据长度（输出）
 */
void ADC_DualSync_GetData(int16_t** ch1_data, int16_t** ch2_data, uint16_t* length);

#endif
