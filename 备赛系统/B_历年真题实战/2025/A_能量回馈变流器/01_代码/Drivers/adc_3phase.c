/**
 * @file    adc_3phase.c
 * @brief   三相 ADC 同步采样实现（占位 + 数据转换）
 *
 *  TODO_HAL：等 CubeMX 完成 ADC1/ADC2 + DMA + TIM1 TRGO 配置后，
 *           填充 _Init / _Start / _Stop 内的 HAL 调用即可。
 */

#include "adc_3phase.h"
#include "../config.h"

/* TODO_HAL: #include "stm32g4xx_hal.h" */
/* TODO_HAL: extern ADC_HandleTypeDef hadc1, hadc2;  */
/* TODO_HAL: extern DMA_HandleTypeDef hdma_adc1;     */

/* DMA 双缓冲（单次 7 通道，2 帧深 → 14 word）*/
#define ADC_DMA_FRAMES   2u
#define ADC_DMA_BUF_LEN  (7u * ADC_DMA_FRAMES)

static volatile uint16_t s_dma_buf[ADC_DMA_BUF_LEN];

void ADC3Phase_Init(void)
{
    /* TODO_HAL:
     *   1) ADC1 配置：4 通道，扫描，TRGO=TIM1_TRGO
     *      通道顺序：Va → Vb → Vc → Vbus
     *   2) ADC2 配置：3 通道，扫描，触发同 ADC1
     *      通道顺序：Ia → Ib → Ic
     *   3) DMA 配置：循环模式，半完成 + 全完成中断
     *   4) TIM1 配置：TRGO 选择 Update Event（每开关周期一次）
     */
}

void ADC3Phase_Start(void)
{
    /* TODO_HAL:
     *   HAL_ADC_Start_DMA(&hadc1, (uint32_t*)s_dma_buf, ADC_DMA_BUF_LEN);
     *   HAL_ADCEx_MultiModeStart_DMA 或类似（取决于具体配置）
     */
}

void ADC3Phase_Stop(void)
{
    /* TODO_HAL: HAL_ADC_Stop_DMA(&hadc1); */
}

/* ---- 转换：raw → 物理量 ---- */
void ADC3Phase_Convert(const ADCSample *raw, ADCConverted *out)
{
    if (!raw || !out) return;

    /* 三相电压：(raw - 2048) / 4096 × 3.3 × 11 */
    for (uint8_t p = 0; p < 3u; p++) {
        int32_t centered = (int32_t)raw->raw_v[p] - (int32_t)ADC_OFFSET_RAW;
        out->v_phase[p] = (float)centered / (float)ADC_RESOLUTION
                          * ADC_VREF_V * V_DIVIDER_RATIO;
    }

    /* 母线电压：单端正向，分压比同样 11:1，但无偏置 */
    out->vbus = (float)raw->raw_vbus / (float)ADC_RESOLUTION
                * ADC_VREF_V * V_DIVIDER_RATIO;

    /* 线电压（注意：题目要求线电压 RMS = 32V）*/
    out->v_line[0] = out->v_phase[0] - out->v_phase[1];   /* Vab */
    out->v_line[1] = out->v_phase[1] - out->v_phase[2];   /* Vbc */
    out->v_line[2] = out->v_phase[2] - out->v_phase[0];   /* Vca */

    /* 三相电流：(V_sense - 2.5V) / 0.185 V/A */
    for (uint8_t p = 0; p < 3u; p++) {
        float v_sense = (float)raw->raw_i[p] / (float)ADC_RESOLUTION * ADC_VREF_V;
        out->i_phase[p] = (v_sense - I_SENSOR_OFFSET_V) / I_SENSOR_GAIN_VA;
    }
}

/* 弱定义：默认空实现，由 Core/main.c 覆盖 */
__attribute__((weak)) void ADC3Phase_OnSampleReady(const ADCConverted *sample)
{
    (void)sample;
}

/* TODO_HAL: HAL ADC 完成回调
 *
 *   void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) {
 *       if (hadc->Instance == ADC1) {
 *           ADCSample raw = parse_dma(s_dma_buf, 0);
 *           ADCConverted phy;
 *           ADC3Phase_Convert(&raw, &phy);
 *           ADC3Phase_OnSampleReady(&phy);
 *       }
 *   }
 *   void HAL_ADC_ConvCpltCallback(...) { 同上，从后半缓冲读取 }
 */
