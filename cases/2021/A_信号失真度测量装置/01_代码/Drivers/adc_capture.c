/**
 * ADC采样模块 —— 实现
 * 
 * ============================================================
 * 【MSP432 ADC14 + DMA + Timer_A 的协作】
 * ============================================================
 * 
 * 数据流：
 *   Timer_A溢出 → 触发ADC14开始转换 → 转换完成 → DMA搬运到内存
 *   → DMA计数减1 → 计数到0时产生中断 → 采集完成
 * 
 * 这三个外设的配合是MSP432编程中最容易出错的地方。
 * 关键配置点：
 * 1. Timer_A的输出模式必须是"Toggle/Reset"或"Set/Reset"，
 *    产生周期性的上升沿触发ADC
 * 2. ADC14的触发源必须设置为对应的Timer_A通道
 * 3. DMA的触发源必须设置为ADC14的转换完成事件
 * 
 * ============================================================
 * 【一个血泪教训：MSP432的DMA和STM32完全不同】
 * ============================================================
 * 
 * STM32的DMA：
 *   配置好源地址、目标地址、传输次数，启动后自动搬运
 *   每次ADC转换完成自动触发一次DMA传输
 *   
 * MSP432的DMA：
 *   也是配置源、目标、次数，但触发方式不同
 *   MSP432用的是μDMA控制器，需要配置"通道控制结构"
 *   而且DMA通道和外设的映射关系是固定的（不像STM32可以灵活配置）
 *   ADC14的DMA通道是DMA_CH7
 * 
 * 如果你之前只用过STM32，这里一定会踩坑。
 * 建议先跑通TI官方的ADC14+DMA示例代码，再改成自己的。
 * 
 * ============================================================
 */

#include "adc_capture.h"
#include <string.h>

/* -------- 缓冲区 -------- */
static uint16_t adc_raw_buf[FFT_SIZE];      // 原始ADC值 (0~16383)
static int16_t  adc_signed_buf[FFT_SIZE];   // 去偏置后 (-8192~+8191)
static volatile uint16_t capture_len = 0;
static volatile uint8_t capture_done = 0;

/* -------- DMA控制结构 -------- */
// MSP432的μDMA需要一个对齐到1024字节边界的控制表
// 这个表通常放在RAM的固定位置
#if defined(__TI_COMPILER_VERSION__)
#pragma DATA_ALIGN(dma_control_table, 1024)
#elif defined(__GNUC__)
__attribute__((aligned(1024)))
#endif
static uint8_t dma_control_table[1024];

/* ============================================================
 * 初始化
 * ============================================================ */

void ADC_Capture_Init(void)
{
    /* ---- GPIO配置：P5.5为模拟输入 ---- */
    MAP_GPIO_setAsPeripheralModuleFunctionInputPin(
        ADC_INPUT_PORT, ADC_INPUT_PIN, GPIO_TERTIARY_MODULE_FUNCTION);
    
    /* ---- ADC14配置 ---- */
    // 使能ADC14，使用SMCLK(24MHz)，单通道单次转换
    MAP_ADC14_enableModule();
    MAP_ADC14_initModule(ADC_CLOCKSOURCE_SMCLK, ADC_PREDIVIDER_1, 
                          ADC_DIVIDER_1, 0);
    
    // 配置采样通道
    MAP_ADC14_configureSingleSampleMode(ADC_MEM0, true);  // true=重复模式
    MAP_ADC14_configureConversionMemory(ADC_MEM0, ADC_VREFPOS_AVCC_VREFNEG_VSS,
                                         ADC_INPUT_CHANNEL, ADC_NONDIFFERENTIAL_INPUTS);
    
    // 14bit分辨率
    MAP_ADC14_setResolution(ADC_14BIT);
    
    // 采样时间：4个ADC时钟（最快）
    MAP_ADC14_setSampleHoldTime(ADC_PULSE_WIDTH_4, ADC_PULSE_WIDTH_4);
    
    /* ---- Timer_A配置（产生ADC触发信号） ---- */
    // 使用Timer_A0的CCR1输出，产生周期性触发
    // 初始周期对应100kHz采样率
    // Timer_A时钟 = SMCLK = 24MHz
    // 周期 = 24MHz / 100kHz = 240
    const Timer_A_UpModeConfig ta_config = {
        TIMER_A_CLOCKSOURCE_SMCLK,
        TIMER_A_CLOCKSOURCE_DIVIDER_1,
        240 - 1,  // 周期（初始值，会被SetRate修改）
        TIMER_A_TAIE_INTERRUPT_DISABLE,
        TIMER_A_CCIE_CCR0_INTERRUPT_DISABLE,
        TIMER_A_DO_CLEAR
    };
    MAP_Timer_A_configureUpMode(TIMER_A0_BASE, &ta_config);
    
    // CCR1输出Toggle/Reset模式，产生触发脉冲
    const Timer_A_CompareModeConfig ta_compare = {
        TIMER_A_CAPTURECOMPARE_REGISTER_1,
        TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE,
        TIMER_A_OUTPUTMODE_SET_RESET,
        120  // 占空比50%
    };
    MAP_Timer_A_initCompare(TIMER_A0_BASE, &ta_compare);
    
    // 设置ADC14的触发源为Timer_A0 CCR1
    MAP_ADC14_setSampleHoldTrigger(ADC_TRIGGER_SOURCE1, false);
    
    /* ---- DMA配置 ---- */
    MAP_DMA_enableModule();
    MAP_DMA_setControlBase(dma_control_table);
    
    // DMA通道7 = ADC14
    MAP_DMA_assignChannel(DMA_CH7_ADC14);
    
    capture_done = 0;
    capture_len = 0;
}

/* ============================================================
 * 设置采样率
 * ============================================================ */

float ADC_Capture_SetRate(float sample_rate_hz)
{
    /*
     * Timer_A周期 = SMCLK / sample_rate
     * SMCLK = 24MHz
     * 
     * sample_rate = 100kHz → 周期 = 240
     * sample_rate = 1MHz → 周期 = 24
     * sample_rate = 10kHz → 周期 = 2400
     * 
     * 【精度分析】
     * 周期必须是整数，所以实际采样率 = 24MHz / round(24MHz/fs)
     * 例：fs=97656Hz → 周期=round(245.76)=246 → 实际fs=97561Hz
     * 误差 = (97656-97561)/97656 = 0.097% → 可以接受
     * 
     * 但对于同步采样，这个误差会导致频谱泄漏！
     * 所以calc_sync_sample_rate()返回的是理论值，
     * 这里返回的是实际值，两者的差异需要用窗函数补偿。
     */
    
    uint32_t period = (uint32_t)(SMCLK_HZ / sample_rate_hz + 0.5f);
    if (period < 24) period = 24;       // 最高1MSPS
    if (period > 60000) period = 60000; // 最低400Hz
    
    // 更新Timer_A周期
    MAP_Timer_A_stopTimer(TIMER_A0_BASE);
    TIMER_A0->CCR[0] = period - 1;
    TIMER_A0->CCR[1] = period / 2;  // 50%占空比
    
    float actual_rate = (float)SMCLK_HZ / (float)period;
    return actual_rate;
}

/* ============================================================
 * 启动采集
 * ============================================================ */

void ADC_Capture_Start(uint16_t num_samples)
{
    if (num_samples > FFT_SIZE) num_samples = FFT_SIZE;
    capture_len = num_samples;
    capture_done = 0;
    
    /*
     * 配置DMA：从ADC14 MEM0搬运到adc_raw_buf
     * 
     * 源地址：ADC14->MEM[0]（ADC转换结果寄存器）
     * 目标地址：adc_raw_buf[0]
     * 传输次数：num_samples
     * 数据宽度：16bit
     * 源地址不递增（始终读同一个寄存器），目标地址递增
     */
    
    MAP_DMA_setChannelControl(
        DMA_CH7_ADC14 | UDMA_PRI_SELECT,
        UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_1);
    
    MAP_DMA_setChannelTransfer(
        DMA_CH7_ADC14 | UDMA_PRI_SELECT,
        UDMA_MODE_BASIC,
        (void*)&ADC14->MEM[0],
        (void*)adc_raw_buf,
        num_samples);
    
    // 使能DMA通道7
    MAP_DMA_enableChannel(7);
    
    // 使能DMA完成中断
    MAP_DMA_enableInterrupt(INT_DMA_INT1);
    MAP_DMA_assignInterrupt(DMA_INT1, 7);
    MAP_Interrupt_enableInterrupt(INT_DMA_INT1);
    
    // 启动ADC转换
    MAP_ADC14_enableConversion();
    
    // 启动Timer_A（开始产生触发脉冲）
    MAP_Timer_A_startCounter(TIMER_A0_BASE, TIMER_A_UP_MODE);
}

/* ============================================================
 * DMA完成中断
 * ============================================================ */

void DMA_INT1_IRQHandler(void)
{
    MAP_DMA_disableChannel(7);
    MAP_Timer_A_stopTimer(TIMER_A0_BASE);
    MAP_ADC14_disableConversion();
    
    // 去偏置：原始值减去中间值
    for (uint16_t i = 0; i < capture_len; i++) {
        adc_signed_buf[i] = (int16_t)adc_raw_buf[i] - ADC_MID_VALUE;
    }
    
    capture_done = 1;
    
    MAP_DMA_clearInterruptFlag(DMA_CH7_ADC14 & 0x0F);
}

/* ============================================================
 * 查询和获取数据
 * ============================================================ */

uint8_t ADC_Capture_IsComplete(void)
{
    return capture_done;
}

void ADC_Capture_GetData(uint16_t** data, uint16_t* length)
{
    *data = adc_raw_buf;
    *length = capture_len;
}

void ADC_Capture_GetSignedData(int16_t** data, uint16_t* length)
{
    *data = adc_signed_buf;
    *length = capture_len;
}
