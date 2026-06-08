/**
 * @file    adc_dual.c
 * @brief   双通道 ADC 同步采样实现（占位 + 数据转换）
 *
 *  TODO_TI：CCS + MSP430 DriverLib 配置后填补 _Init / _StartOnce 等函数体。
 */

#include "adc_dual.h"

/* TODO_TI: #include <ti/devices/msp430fr5xx_6xx/driverlib/MSP430FR5xx_6xx/driverlib.h> */

/* DMA 双缓冲（V/I 交错）*/
static volatile uint16_t s_dma_buf[DFT_N * 2];
static volatile uint8_t  s_done = 0;

/* 校准系数（运行时可由校准流程更新）*/
static float s_v_gain   = V_GAIN_INIT;
static float s_v_offset = V_OFFSET_INIT;
static float s_i_gain   = I_GAIN_INIT;
static float s_i_offset = I_OFFSET_INIT;

void ADCDual_Init(void)
{
    /* TODO_TI:
     *  1) ADC12_B：12bit、参考电压 = REF3030 = 3.0V（或 VCC = 3.3V）
     *     扫描通道 A0 → A1，单次序列模式
     *     触发源 = TimerA0 CCR1
     *  2) Timer_A0：周期 = SMCLK / SAMPLE_RATE_HZ - 1
     *     CCR1 输出 Set/Reset 模式做触发脉冲
     *  3) DMA 通道 0：源=ADC12->MEM[0]，目=s_dma_buf，
     *     传输次数 = DFT_N × 2，宽度 = 16bit
     *  4) 启用 DMA 完成中断
     */
    s_done = 0;
}

void ADCDual_StartOnce(void)
{
    s_done = 0;
    /* TODO_TI:
     *   - 复位 DMA 计数为 DFT_N × 2
     *   - 使能 ADC12_B
     *   - 启动 Timer_A0
     *   完成后 DMA 中断置 s_done = 1
     */
}

void ADCDual_Stop(void)
{
    /* TODO_TI: 关 Timer + 关 ADC + 关 DMA */
    s_done = 0;
}

uint8_t ADCDual_IsDone(void)
{
    return s_done;
}

/* ---- 把 DMA 原始数据转为物理量 ---- */
void ADCDual_GetFrame(ADCConvertedFrame *frame)
{
    if (!frame) return;

    for (uint16_t k = 0; k < DFT_N; k++) {
        int32_t v_raw = (int32_t)s_dma_buf[2u * k]     - (int32_t)ADC_OFFSET_RAW;
        int32_t i_raw = (int32_t)s_dma_buf[2u * k + 1] - (int32_t)ADC_OFFSET_RAW;

        frame->v_samples[k] = (float)v_raw * s_v_gain + s_v_offset;
        frame->i_samples[k] = (float)i_raw * s_i_gain + s_i_offset;
    }
}

/* ---- 校准接口（由 main 在收到校准命令时调用，写到 FRAM 持久保存）---- */
void ADCDual_SetCalibration(float v_gain, float v_offset, float i_gain, float i_offset)
{
    s_v_gain   = v_gain;
    s_v_offset = v_offset;
    s_i_gain   = i_gain;
    s_i_offset = i_offset;
}

const uint16_t *ADCDual_GetRawBuffer(void)
{
    return (const uint16_t *)s_dma_buf;
}
