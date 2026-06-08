/**
 * @file    adc_us_il.c
 * @brief   STM32F407 双 ADC 同步采样驱动 —— 占位实现
 *
 * 真机移植步骤：
 *   1. 用 CubeMX 配置 ADC1+ADC2 同步双模式 / TIM1 TRGO 触发 / DMA1 ST0 ⇒ 替换 _hal_init() 内空骨架
 *   2. ADC ISR 内调用 _on_dma_complete() 推帧
 *   3. RMS / 物理量换算函数已经在本文件，可直接保留
 *
 * 当前文件可与算法层一同 PC 端编译通过（不依赖 HAL）。
 */

#include "adc_us_il.h"
#include "../config.h"
#include <string.h>

/* ========================================================================
 * 静态状态
 * ===================================================================== */

static uint16_t s_ring_buf[HARMONIC_FFT_SIZE];
static volatile uint32_t s_ring_idx = 0;
static volatile uint8_t  s_frame_ready = 0;

static float s_us_offset = ADC_US_OFFSET_LSB;
static float s_il_offset = ADC_IL_OFFSET_LSB;
static float s_if_offset = ADC_IF_OFFSET_LSB;

static volatile uint8_t s_running = 0;

/* 双 ADC 当前规整结果：实际项目里在 ADC ISR 写入 */
static volatile uint16_t s_us_raw = 2048;
static volatile uint16_t s_il_raw = 2048;
static volatile uint16_t s_if_raw = 2048;
static volatile uint8_t  s_sample_new = 0;

/* ========================================================================
 * HAL 钩子（占位）
 * ===================================================================== */

static void _hal_init(void)
{
    /* TODO: CubeMX 生成的 MX_ADC1_Init / MX_ADC2_Init / MX_DMA_Init / MX_TIM1_Init */
}

static void _hal_start(void)
{
    /* TODO: HAL_ADC_Start_DMA + HAL_TIM_Base_Start */
}

static void _hal_stop(void)
{
    /* TODO: HAL_ADC_Stop_DMA + HAL_TIM_Base_Stop */
}

/* ========================================================================
 * 公共 API
 * ===================================================================== */

void adc_us_il_init(void)
{
    s_ring_idx     = 0;
    s_frame_ready  = 0;
    s_sample_new   = 0;
    s_running      = 0;
    s_us_offset    = ADC_US_OFFSET_LSB;
    s_il_offset    = ADC_IL_OFFSET_LSB;
    s_if_offset    = ADC_IF_OFFSET_LSB;
    _hal_init();
}

void adc_us_il_start(void)
{
    s_running = 1;
    _hal_start();
}

void adc_us_il_stop(void)
{
    s_running = 0;
    _hal_stop();
}

void adc_us_il_auto_zero(void)
{
    /* 调用方应保证调用时无外部信号注入。
     * 这里只是保留接口框架——真机时累加 ADC_AUTO_ZERO_SAMPLES 个采样取平均。
     */
    uint32_t sum_us = 0, sum_il = 0, sum_if = 0;
    for (uint32_t i = 0; i < ADC_AUTO_ZERO_SAMPLES; i++) {
        /* 真机：在 ADC ISR 里轮询 s_sample_new 拉数据 */
        sum_us += s_us_raw;
        sum_il += s_il_raw;
        sum_if += s_if_raw;
    }
    s_us_offset = (float)sum_us / (float)ADC_AUTO_ZERO_SAMPLES;
    s_il_offset = (float)sum_il / (float)ADC_AUTO_ZERO_SAMPLES;
    s_if_offset = (float)sum_if / (float)ADC_AUTO_ZERO_SAMPLES;
}

uint8_t adc_us_il_get_sample(adc_sample_t *out)
{
    if ((out == 0) || (s_sample_new == 0)) {
        return 0;
    }
    out->us  = ((float)s_us_raw - s_us_offset) * ADC_US_SCALE_V_PER_LSB;
    out->il  = ((float)s_il_raw - s_il_offset) * ADC_IL_SCALE_A_PER_LSB;
    out->i_f = ((float)s_if_raw - s_if_offset) * ADC_IF_SCALE_A_PER_LSB;
    s_sample_new = 0;
    return 1;
}

uint8_t adc_us_il_get_frame(uint16_t *buf, uint32_t len)
{
    if ((buf == 0) || (len < HARMONIC_FFT_SIZE) || (s_frame_ready == 0)) {
        return 0;
    }
    memcpy(buf, (const void *)s_ring_buf, sizeof(uint16_t) * HARMONIC_FFT_SIZE);
    s_frame_ready = 0;
    return 1;
}

/* ========================================================================
 * 中断回调钩子（真机 ISR 调用）
 * ===================================================================== */

void adc_us_il_on_sample(uint16_t us_raw, uint16_t il_raw, uint16_t if_raw)
{
    /* 1. 更新最新瞬时值 */
    s_us_raw     = us_raw;
    s_il_raw     = il_raw;
    s_if_raw     = if_raw;
    s_sample_new = 1;

    /* 2. iL 进谐波 FFT 环形缓冲 */
    if (s_running) {
        s_ring_buf[s_ring_idx++] = il_raw;
        if (s_ring_idx >= HARMONIC_FFT_SIZE) {
            s_ring_idx    = 0;
            s_frame_ready = 1;
        }
    }
}
