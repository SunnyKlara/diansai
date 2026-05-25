/**
 * DAC连续输出模块 —— 头文件
 * 
 * 使用DMA+定时器触发，实现DAC的连续波形输出。
 * 双缓冲机制：一个缓冲区正在输出，另一个正在被填充新数据。
 */

#ifndef __DAC_OUTPUT_H
#define __DAC_OUTPUT_H

#include "stm32f4xx_hal.h"
#include "config.h"

/**
 * 初始化DAC输出
 * 配置DAC1_OUT1(PA4)，TIM6触发，DMA传输
 */
void DAC_Out_Init(void);

/**
 * 设置DAC输出速率
 * @param rate_hz  输出速率(Hz)，应等于ADC采样率
 */
void DAC_Out_SetRate(float rate_hz);

/**
 * 启动DAC连续输出
 * 使用双缓冲：半传输完成中断填充前半部分，全传输完成中断填充后半部分
 */
void DAC_Out_Start(void);

/**
 * 停止DAC输出
 */
void DAC_Out_Stop(void);

/**
 * 获取当前可写入的缓冲区指针
 * @param buf     缓冲区指针（输出）
 * @param length  可写入长度（输出）
 * @return 1=有可写入的缓冲区, 0=没有（上一帧还没输出完）
 */
uint8_t DAC_Out_GetWriteBuffer(uint16_t** buf, uint16_t* length);

/**
 * 通知写入完成（数据已填充到缓冲区）
 */
void DAC_Out_WriteComplete(void);

#endif
