/**
 * @file    adc_sample.c
 * @brief   ADC 采样实现（DMA 双通道 + RMS）
 *
 *  TODO_HAL：CubeMX 配置 ADC1 + DMA + TIM1 TRGO 后，填补 HAL 调用。
 */

#include "adc_sample.h"
#include "../config.h"
#include <math.h>

/* TODO_HAL: #include "stm32g4xx_hal.h" */
/* TODO_HAL: extern ADC_HandleTypeDef hadc1; */
/* TODO_HAL: extern DMA_HandleTypeDef hdma_adc1; */

/* DMA 缓冲（V/I 交错，一周期 400 点 × 2 通道 = 800 点）*/
static volatile uint16_t s_dma_buf[SAMPLES_PER_CYCLE * 2];

void ADC_Sample_Init(void)
{
    /* TODO_HAL:
     *  1) ADC1 12bit，扫描 IN1 / IN2，TRGO = TIM1 Update
     *  2) DMA 循环模式，半 / 全完成中断
     *  3) 通道间隔 ~ 40 ns（双 ADC 同步模式时几乎 0 偏差）
     */
}

void ADC_Sample_Start(void)
{
    /* TODO_HAL:
     *   HAL_ADC_Start_DMA(&hadc1, (uint32_t*)s_dma_buf, SAMPLES_PER_CYCLE * 2);
     */
}

void ADC_Sample_Stop(void)
{
    /* TODO_HAL: HAL_ADC_Stop_DMA(&hadc1); */
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
        float v_sense = (float)s_dma_buf[i * 2u + 1u] / (float)ADC_RESOLUTION * ADC_VREF_V;
        float i_amp   = (v_sense - I_SENSOR_OFFSET_V) / I_SENSOR_GAIN_VA;
        sum_sq += (double)i_amp * (double)i_amp;
    }
    return (float)sqrt(sum_sq / (double)SAMPLES_PER_CYCLE);
}
