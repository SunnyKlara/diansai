/**
 * @file    adc_sample.c
 * @brief   STM32G474 ADC1 双通道 + DMA + RMS 计算
 *
 * 资源映射：
 *   ADC1 IN1 (PA0) → V_out（电压互感器调理后）
 *   ADC1 IN2 (PA1) → I_out（电流互感器调理后）
 *   触发：TIM1_TRGO（每开关周期一次，20kHz）
 *   DMA：DMA1 Stream0，循环模式，半完成 + 完成中断
 *
 * CubeMX 已生成 hadc1 + hdma_adc1 句柄。
 */

#include "adc_sample.h"
#include "../config.h"
#include "stm32g4xx_hal.h"
#include <math.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

/* DMA 缓冲：V/I 交替排列，一周期 SAMPLES_PER_CYCLE 点 × 2 通道 */
static volatile uint16_t s_dma_buf[SAMPLES_PER_CYCLE * 2u];

void ADC_Sample_Init(void)
{
    /* CubeMX 已配置 ADC1 12bit，扫描 IN1 / IN2，TIM1 TRGO 触发，DMA 循环。
     * 这里启动 ADC 校准，提高精度。
     */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
}

void ADC_Sample_Start(void)
{
    /* 启动 DMA：硬件触发后会自动按 V → I 顺序写入 s_dma_buf */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_dma_buf, SAMPLES_PER_CYCLE * 2u);
}

void ADC_Sample_Stop(void)
{
    HAL_ADC_Stop_DMA(&hadc1);
}

float ADC_Sample_CalcVrms(void)
{
    double sum_sq = 0.0;
    for (uint16_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
        int32_t raw = (int32_t)s_dma_buf[i * 2u] - (int32_t)ADC_OFFSET_RAW;
        float v = (float)raw * V_SCALE;
        sum_sq += (double)v * (double)v;
    }
    return (float)sqrt(sum_sq / (double)SAMPLES_PER_CYCLE);
}

float ADC_Sample_CalcIrms(void)
{
    double sum_sq = 0.0;
    for (uint16_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
        float v_sense = (float)s_dma_buf[i * 2u + 1u]
                        / (float)ADC_RESOLUTION * ADC_VREF_V;
        float i_amp   = (v_sense - I_SENSOR_OFFSET_V) / I_SENSOR_GAIN_VA;
        sum_sq += (double)i_amp * (double)i_amp;
    }
    return (float)sqrt(sum_sq / (double)SAMPLES_PER_CYCLE);
}
