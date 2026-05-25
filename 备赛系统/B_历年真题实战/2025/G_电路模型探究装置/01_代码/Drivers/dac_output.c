/**
 * DAC连续输出模块 —— 实现
 * 
 * ============================================================
 * 【双缓冲的必要性】
 * ============================================================
 * 
 * 推理阶段需要连续不断地输出信号。
 * 如果只有一个缓冲区：
 *   输出完一帧 → 停下来等处理 → 处理完再输出 → 输出有间断
 *   间断会在示波器上表现为波形跳变，不满足"连续稳定显示"的要求。
 * 
 * 双缓冲方案：
 *   缓冲区A正在DMA输出 → 同时CPU在处理并填充缓冲区B
 *   缓冲区A输出完 → 无缝切换到缓冲区B → CPU开始填充缓冲区A
 *   → 输出永远不间断
 * 
 * STM32的DMA循环模式天然支持双缓冲：
 *   配置DMA为循环模式，缓冲区大小=2×帧长度
 *   DMA半传输完成中断 → 前半部分输出完，可以填充前半部分
 *   DMA全传输完成中断 → 后半部分输出完，可以填充后半部分
 * 
 * ============================================================
 */

#include "dac_output.h"

/* 双缓冲区（前半部分和后半部分交替使用） */
static uint16_t dac_buffer[FFT_SIZE * 2];  // 总长度 = 2帧

/* 状态标志 */
static volatile uint8_t front_half_ready = 0;  // 前半部分可以写入
static volatile uint8_t back_half_ready = 0;   // 后半部分可以写入
static volatile uint8_t running = 0;

extern DAC_HandleTypeDef hdac;
extern TIM_HandleTypeDef htim6;

void DAC_Out_Init(void)
{
    /*
     * CubeMX配置要点：
     * 
     * DAC1:
     *   Output1: Enable
     *   Trigger: Timer 6 Trigger Out event
     *   Output Buffer: Enable
     *   
     * TIM6:
     *   Clock Source: Internal
     *   Trigger Output: Update Event
     *   PSC和ARR由DAC_Out_SetRate()设置
     *   
     * DMA:
     *   DMA1 Stream5, Channel7 (DAC1)
     *   Direction: Memory to Peripheral
     *   Data Width: Half Word (16bit)
     *   Mode: Circular ← 关键！循环模式实现双缓冲
     */
    
    // 初始化缓冲区为中间值（静音）
    for (uint32_t i = 0; i < FFT_SIZE * 2; i++) {
        dac_buffer[i] = 2048;  // DAC中间值 = 0V
    }
    
    running = 0;
    front_half_ready = 0;
    back_half_ready = 0;
}

void DAC_Out_SetRate(float rate_hz)
{
    /*
     * TIM6时钟 = APB1 × 2 = 84MHz
     * DAC输出率 = TIM6_CLK / (PSC+1) / (ARR+1)
     * 
     * 固定PSC=0：
     *   ARR = 84000000 / rate_hz - 1
     *   rate=200kHz → ARR = 419
     */
    uint32_t arr = (uint32_t)(84000000.0f / rate_hz + 0.5f) - 1;
    if (arr < 10) arr = 10;
    if (arr > 65535) arr = 65535;
    
    htim6.Instance->PSC = 0;
    htim6.Instance->ARR = arr;
}

void DAC_Out_Start(void)
{
    running = 1;
    front_half_ready = 1;  // 第一帧：前半部分可以写入
    back_half_ready = 0;
    
    // 启动DAC DMA循环输出
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, 
                       (uint32_t*)dac_buffer, FFT_SIZE * 2, 
                       DAC_ALIGN_12B_R);
    
    // 启动TIM6触发
    HAL_TIM_Base_Start(&htim6);
}

void DAC_Out_Stop(void)
{
    running = 0;
    HAL_TIM_Base_Stop(&htim6);
    HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);
}

/*
 * DMA半传输完成回调
 * 前半部分(0 ~ FFT_SIZE-1)已经输出完毕，可以被覆写
 */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef* hdac_ptr)
{
    (void)hdac_ptr;
    front_half_ready = 1;
}

/*
 * DMA全传输完成回调
 * 后半部分(FFT_SIZE ~ 2*FFT_SIZE-1)已经输出完毕，可以被覆写
 */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac_ptr)
{
    (void)hdac_ptr;
    back_half_ready = 1;
}

uint8_t DAC_Out_GetWriteBuffer(uint16_t** buf, uint16_t* length)
{
    if (front_half_ready) {
        *buf = &dac_buffer[0];
        *length = FFT_SIZE;
        return 1;
    }
    if (back_half_ready) {
        *buf = &dac_buffer[FFT_SIZE];
        *length = FFT_SIZE;
        return 1;
    }
    return 0;  // 没有可写入的缓冲区
}

void DAC_Out_WriteComplete(void)
{
    if (front_half_ready) {
        front_half_ready = 0;
    } else if (back_half_ready) {
        back_half_ready = 0;
    }
}
