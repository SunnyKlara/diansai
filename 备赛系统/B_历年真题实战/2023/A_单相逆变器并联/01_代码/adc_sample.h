#ifndef __ADC_SAMPLE_H
#define __ADC_SAMPLE_H

#include "stm32g4xx_hal.h"

// 每个正弦周期采样点数 = 开关频率/输出频率 = 400
#define SAMPLES_PER_CYCLE  400

// 电压采样缩放系数 (根据分压电阻计算)
// 假设分压比 = 1:10, ADC参考电压3.3V, 12bit
// 实际电压 = ADC值 / 4096 * 3.3 * 11
#define VOLTAGE_SCALE  (3.3f / 4096.0f * 11.0f)

// 电流采样缩放系数 (ACS712-5A: 185mV/A, 偏置2.5V)
// 实际电流 = (ADC电压 - 2.5) / 0.185
#define CURRENT_OFFSET 2.5f
#define CURRENT_SCALE  (1.0f / 0.185f)

void ADC_Sample_Init(ADC_HandleTypeDef* hadc, DMA_HandleTypeDef* hdma);
void ADC_Sample_Start(void);
float ADC_Sample_CalcVrms(void);
float ADC_Sample_CalcIrms(void);

#endif
