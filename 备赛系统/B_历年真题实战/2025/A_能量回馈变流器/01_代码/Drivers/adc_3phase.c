/**
 * @file    adc_3phase.c
 * @brief   STM32G474 三相 ADC 同步采样（ADC1 + ADC2 双模式 + DMA）
 *
 * 资源映射：
 *   ADC1 顺序：Va(PA3, IN4) → Vb(PA4, IN17) → Vbus(PC0, IN6)
 *   ADC2 顺序：Ia(PA0, IN1) → Ib(PA1, IN2) → Ic(PA2, IN3) → Vc(PA5, IN13)
 *   触发：TIM1_TRGO（PWM 中点，20kHz）
 *   DMA：DMA1 Stream0，循环 + 半 / 全完成中断
 *
 * 数据布局（s_dma_buf 一帧 = 7 word）：
 *   [0]=Va, [1]=Vb, [2]=Vbus, [3]=Ia, [4]=Ib, [5]=Ic, [6]=Vc
 *   双缓冲 → 14 word
 */

#include "adc_3phase.h"
#include "../config.h"
#include "stm32g4xx_hal.h"

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern DMA_HandleTypeDef hdma_adc1;

#define ADC_DMA_FRAMES   2u
#define ADC_DMA_BUF_LEN  (7u * ADC_DMA_FRAMES)

static volatile uint16_t s_dma_buf[ADC_DMA_BUF_LEN];

/* 解析一帧 DMA → ADCSample */
static void parse_frame(uint8_t frame_idx, ADCSample *out)
{
    const volatile uint16_t *p = &s_dma_buf[frame_idx * 7u];
    out->raw_v[0]  = p[0];   /* Va   */
    out->raw_v[1]  = p[1];   /* Vb   */
    out->raw_vbus  = p[2];   /* Vbus */
    out->raw_i[0]  = p[3];   /* Ia   */
    out->raw_i[1]  = p[4];   /* Ib   */
    out->raw_i[2]  = p[5];   /* Ic   */
    out->raw_v[2]  = p[6];   /* Vc   */
}

void ADC3Phase_Init(void)
{
    /* CubeMX 已配置 ADC1+ADC2 同步双模式 + DMA。
     * 启动两个 ADC 校准。
     */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
}

void ADC3Phase_Start(void)
{
    /* 启动多模式 DMA：ADC1 主，ADC2 跟随 */
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)s_dma_buf, ADC_DMA_BUF_LEN);
}

void ADC3Phase_Stop(void)
{
    HAL_ADCEx_MultiModeStop_DMA(&hadc1);
}

void ADC3Phase_Convert(const ADCSample *raw, ADCConverted *out)
{
    if (!raw || !out) return;

    /* 三相电压：(raw - 2048) / 4096 × 3.3 × 11 */
    for (uint8_t p = 0; p < 3u; p++) {
        int32_t centered = (int32_t)raw->raw_v[p] - (int32_t)ADC_OFFSET_RAW;
        out->v_phase[p] = (float)centered / (float)ADC_RESOLUTION
                          * ADC_VREF_V * V_DIVIDER_RATIO;
    }

    /* 母线电压：单端正向，分压比 11:1，无偏置 */
    out->vbus = (float)raw->raw_vbus / (float)ADC_RESOLUTION
                * ADC_VREF_V * V_DIVIDER_RATIO;

    /* 线电压 */
    out->v_line[0] = out->v_phase[0] - out->v_phase[1];
    out->v_line[1] = out->v_phase[1] - out->v_phase[2];
    out->v_line[2] = out->v_phase[2] - out->v_phase[0];

    /* 三相电流：(V_sense - 2.5V) / 0.185 V/A */
    for (uint8_t p = 0; p < 3u; p++) {
        float v_sense = (float)raw->raw_i[p] / (float)ADC_RESOLUTION * ADC_VREF_V;
        out->i_phase[p] = (v_sense - I_SENSOR_OFFSET_V) / I_SENSOR_GAIN_VA;
    }
}

/* 弱定义，由 Core/main.c 重载 */
__attribute__((weak)) void ADC3Phase_OnSampleReady(const ADCConverted *sample)
{
    (void)sample;
}

/* DMA 完成回调：CubeMX 自动注册到 HAL 层 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        ADCSample raw; ADCConverted phy;
        parse_frame(0, &raw);
        ADC3Phase_Convert(&raw, &phy);
        ADC3Phase_OnSampleReady(&phy);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        ADCSample raw; ADCConverted phy;
        parse_frame(1, &raw);
        ADC3Phase_Convert(&raw, &phy);
        ADC3Phase_OnSampleReady(&phy);
    }
}
