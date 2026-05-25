/**
 * ADC双通道同步采样模块 —— 实现
 * 
 * ============================================================
 * 【STM32F407双ADC同步模式的工作原理】
 * ============================================================
 * 
 * STM32F407有3个ADC（ADC1/2/3），支持多种多ADC模式：
 * - 双ADC同步规则模式：ADC1和ADC2同时采样不同通道
 * - 三ADC交替模式：三个ADC轮流采样同一通道（提高采样率）
 * 
 * 本项目使用"双ADC同步规则模式"：
 *   ADC1采样PA0（输入信号）
 *   ADC2采样PA1（输出信号）
 *   TIM2 TRGO事件同时触发两个ADC
 *   DMA将两个ADC的结果打包传输到内存
 * 
 * 数据格式（双ADC模式下DMA传输的是32bit数据）：
 *   [31:16] = ADC2结果
 *   [15:0]  = ADC1结果
 * 
 * ============================================================
 * 【DMA配置的关键细节】
 * ============================================================
 * 
 * 双ADC模式下，DMA请求由ADC1的公共数据寄存器(CDR)产生。
 * 不是ADC1和ADC2各自的DMA请求！
 * 
 * 这意味着：
 * 1. 只需要配置一个DMA通道（DMA2_Stream0，ADC1的DMA）
 * 2. DMA源地址是ADC->CDR（公共数据寄存器），不是ADC1->DR
 * 3. DMA数据宽度是32bit（包含两个ADC的结果）
 * 
 * 很多人在这里踩坑：配置了两个DMA通道分别给ADC1和ADC2，
 * 结果数据错乱。双ADC模式下只能用一个DMA通道读CDR。
 * 
 * ============================================================
 */

#include "adc_dual_sync.h"
#include <string.h>

/* -------- 内部缓冲区 -------- */
#define MAX_SAMPLES 2048

// DMA接收缓冲区（32bit：高16位=ADC2，低16位=ADC1）
static uint32_t dma_raw_buf[MAX_SAMPLES];

// 拆分后的数据（去除2048偏置，转为有符号数）
static int16_t ch1_buf[MAX_SAMPLES];  // ADC1 = 输入信号
static int16_t ch2_buf[MAX_SAMPLES];  // ADC2 = 输出信号

static volatile uint16_t capture_len = 0;
static volatile uint8_t capture_complete = 0;

/* -------- 外设句柄（由CubeMX生成，这里声明外部引用） -------- */
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

/* -------- 初始化 -------- */

void ADC_DualSync_Init(void)
{
    /*
     * CubeMX配置要点（必须在CubeMX中正确设置）：
     * 
     * ADC1:
     *   Mode: Dual regular simultaneous mode only
     *   DMA Continuous Requests: Enable
     *   External Trigger: Timer 2 Trigger Out event
     *   Channel: IN0 (PA0)
     *   Sampling Time: 15 cycles（快速采样）
     *   Resolution: 12 bit
     *   
     * ADC2:
     *   Mode: (由ADC1的多ADC模式自动配置)
     *   Channel: IN1 (PA1)
     *   Sampling Time: 15 cycles
     *   
     * DMA:
     *   DMA2 Stream0, Channel0 (ADC1)
     *   Direction: Peripheral to Memory
     *   Data Width: Word (32bit) ← 关键！不是Half Word
     *   Mode: Normal（采集指定数量后停止）
     *   
     * TIM2:
     *   Clock Source: Internal
     *   Trigger Output: Update Event
     *   PSC和ARR由ADC_DualSync_SetRate()动态设置
     */
    
    capture_complete = 0;
    capture_len = 0;
}

/* -------- 设置采样率 -------- */

float ADC_DualSync_SetRate(float sample_rate_hz)
{
    /*
     * TIM2时钟 = APB1 × 2 = 42MHz × 2 = 84MHz
     * （APB1分频系数>1时，定时器时钟自动×2）
     * 
     * 采样率 = TIM2_CLK / (PSC+1) / (ARR+1)
     * 
     * 为了最大灵活性，固定PSC=0，只调ARR：
     *   ARR = TIM2_CLK / sample_rate - 1
     *   
     * sample_rate=200kHz → ARR = 84000000/200000 - 1 = 419
     * sample_rate=100kHz → ARR = 84000000/100000 - 1 = 839
     * sample_rate=10kHz  → ARR = 84000000/10000 - 1 = 8399
     */
    
    uint32_t tim_clk = 84000000;  // TIM2时钟84MHz
    uint32_t arr = (uint32_t)(tim_clk / sample_rate_hz + 0.5f) - 1;
    
    if (arr < 10) arr = 10;       // 最高采样率约8.4MHz（实际ADC跟不上）
    if (arr > 65535) arr = 65535;  // 最低采样率约1.28kHz
    
    htim2.Instance->PSC = 0;
    htim2.Instance->ARR = arr;
    htim2.Instance->CNT = 0;
    
    // 返回实际采样率
    float actual_rate = (float)tim_clk / (float)(arr + 1);
    return actual_rate;
}

/* -------- 启动采集 -------- */

void ADC_DualSync_StartCapture(uint16_t num_samples)
{
    if (num_samples > MAX_SAMPLES) num_samples = MAX_SAMPLES;
    
    capture_len = num_samples;
    capture_complete = 0;
    
    // 启动DMA传输（从ADC CDR读取32bit数据到dma_raw_buf）
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, dma_raw_buf, num_samples);
    
    // 启动TIM2触发
    HAL_TIM_Base_Start(&htim2);
}

/* -------- DMA传输完成回调 -------- */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1) {
        // 停止TIM2和ADC
        HAL_TIM_Base_Stop(&htim2);
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        
        // 拆分双ADC数据并去除偏置
        for (uint16_t i = 0; i < capture_len; i++) {
            uint16_t adc1_raw = (uint16_t)(dma_raw_buf[i] & 0xFFFF);
            uint16_t adc2_raw = (uint16_t)((dma_raw_buf[i] >> 16) & 0xFFFF);
            
            /*
             * 去除直流偏置：
             * 信号经过偏置电路后，0V对应ADC中间值(2048)
             * 正半周 > 2048，负半周 < 2048
             * 减去2048得到有符号数，范围约-2048~+2047
             */
            ch1_buf[i] = (int16_t)adc1_raw - 2048;
            ch2_buf[i] = (int16_t)adc2_raw - 2048;
        }
        
        capture_complete = 1;
    }
}

/* -------- 查询和获取数据 -------- */

uint8_t ADC_DualSync_IsComplete(void)
{
    return capture_complete;
}

void ADC_DualSync_GetData(int16_t** ch1_data, int16_t** ch2_data, uint16_t* length)
{
    *ch1_data = ch1_buf;
    *ch2_data = ch2_buf;
    *length = capture_len;
}
