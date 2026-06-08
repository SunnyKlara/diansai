/**
 * AD9833 DDS驱动 —— 头文件
 * 
 * AD9833是Analog Devices的低功耗可编程波形发生器。
 * 输出正弦波、三角波、方波，频率分辨率0.1Hz，最高12.5MHz。
 * SPI接口，3线制（SCLK, SDATA, FSYNC）。
 */

#ifndef __DDS_AD9833_H
#define __DDS_AD9833_H

#include "stm32f4xx_hal.h"

typedef enum {
    DDS_WAVE_SINE,      // 正弦波
    DDS_WAVE_TRIANGLE,  // 三角波
    DDS_WAVE_SQUARE     // 方波（MSB输出）
} DDS_Waveform_t;

/**
 * 初始化AD9833
 * @param hspi  SPI句柄指针
 * 
 * 初始化后AD9833处于复位状态，输出为0。
 * 需要调用DDS_SetFreq()和DDS_Start()才会有输出。
 */
void DDS_Init(SPI_HandleTypeDef* hspi);

/**
 * 设置输出频率
 * @param freq_hz  目标频率(Hz)，范围0.1~12500000
 * 
 * 【精度分析】
 * AD9833的频率寄存器是28bit，参考时钟25MHz时：
 * 频率分辨率 = 25MHz / 2^28 = 0.0931Hz
 * 
 * 设置1kHz时的实际频率：
 *   freq_reg = round(1000 × 2^28 / 25000000) = 10737
 *   实际频率 = 10737 × 25000000 / 2^28 = 999.9953Hz
 *   误差 = 0.005Hz，完全可以忽略
 * 
 * 设置50kHz时：
 *   freq_reg = round(50000 × 2^28 / 25000000) = 536871
 *   实际频率 = 536871 × 25000000 / 2^28 = 49999.977Hz
 *   误差 = 0.023Hz，同样可以忽略
 */
void DDS_SetFreq(float freq_hz);

/**
 * 设置输出波形
 * @param wave  波形类型
 */
void DDS_SetWaveform(DDS_Waveform_t wave);

/**
 * 启动输出（退出复位状态）
 */
void DDS_Start(void);

/**
 * 停止输出（进入复位状态，输出归零）
 */
void DDS_Stop(void);

/**
 * 设置相位偏移
 * @param phase_deg  相位(度)，0~360
 * 
 * 本题中不需要相位控制，但保留接口以备不时之需。
 */
void DDS_SetPhase(float phase_deg);

#endif /* __DDS_AD9833_H */
