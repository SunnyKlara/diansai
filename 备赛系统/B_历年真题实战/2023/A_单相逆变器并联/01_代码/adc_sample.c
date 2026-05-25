/**
 * ADC采样模块
 * 使用DMA双缓冲采集输出电压和电流
 * 每个正弦周期(20ms)采集400个点
 */

#include "adc_sample.h"
#include <math.h>

// DMA缓冲区 (双通道交替: [V0,I0,V1,I1,...])
static uint16_t adc_dma_buf[SAMPLES_PER_CYCLE * 2];

static ADC_HandleTypeDef* p_hadc;

void ADC_Sample_Init(ADC_HandleTypeDef* hadc, DMA_HandleTypeDef* hdma)
{
    p_hadc = hadc;
}

void ADC_Sample_Start(void)
{
    // 启动ADC DMA连续采样
    HAL_ADC_Start_DMA(p_hadc, (uint32_t*)adc_dma_buf, SAMPLES_PER_CYCLE * 2);
}

/**
 * 计算输出电压有效值
 * 从DMA缓冲区中提取电压通道数据，计算RMS
 */
float ADC_Sample_CalcVrms(void)
{
    float sum_sq = 0;
    
    for (uint16_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
        // 电压在偶数位置 [0,2,4,...]
        float adc_val = (float)adc_dma_buf[i * 2];
        float voltage = adc_val * VOLTAGE_SCALE;
        
        // 去除直流偏置 (如果有的话)
        // voltage -= dc_offset;
        
        sum_sq += voltage * voltage;
    }
    
    return sqrtf(sum_sq / SAMPLES_PER_CYCLE);
}

/**
 * 计算输出电流有效值
 */
float ADC_Sample_CalcIrms(void)
{
    float sum_sq = 0;
    
    for (uint16_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
        // 电流在奇数位置 [1,3,5,...]
        float adc_val = (float)adc_dma_buf[i * 2 + 1];
        float adc_voltage = adc_val / 4096.0f * 3.3f;
        float current = (adc_voltage - CURRENT_OFFSET) * CURRENT_SCALE;
        
        sum_sq += current * current;
    }
    
    return sqrtf(sum_sq / SAMPLES_PER_CYCLE);
}
