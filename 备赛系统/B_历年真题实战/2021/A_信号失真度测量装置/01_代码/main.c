/**
 * 2021A 信号失真度测量装置 —— 主程序
 * MCU: TI MSP432P401R
 * 
 * ============================================================
 * 【一键测量的完整流程】
 * ============================================================
 * 
 * 按下启动键后，装置自动执行以下步骤：
 * 
 * 1. [COARSE]  用100kHz采样率采集1024点（10ms）
 * 2. [FREQ]    过零检测法粗测频率（<1ms）
 * 3. [RANGE]   检查信号幅度，必要时切换量程（<1ms）
 * 4. [SYNC]    计算同步采样率（<1ms）
 * 5. [CAPTURE] 用同步采样率重新采集1024点（10~50ms）
 * 6. [FFT]     加平顶窗 → FFT → 幅度谱（<10ms）
 * 7. [THD]     提取谐波 → 计算THD和归一化幅值（<1ms）
 * 8. [DISPLAY] OLED显示THD、波形、谐波柱状图（<100ms）
 * 9. [BT]      蓝牙发送到手机（<200ms）
 * 10.[DONE]    蜂鸣器响一声，LED变绿
 * 
 * 总时间：<400ms，远小于10秒限制
 * 
 * 【为什么这么快？】
 * 因为每一步都是精确计算过的，没有多余的等待。
 * 很多队伍的装置需要5~8秒，是因为他们在FFT之前做了
 * 多次采样取平均，或者用了更大的FFT点数。
 * 我们通过同步采样+平顶窗，一次采样就够了。
 * 
 * 【剩余的9.6秒可以做什么？】
 * 做多次测量取平均！
 * 重复步骤5~7共10次，对THD取平均值。
 * 10次平均可以将随机误差降低√10≈3.2倍。
 * 总时间：400ms + 10×70ms = 1.1秒，仍然远小于10秒。
 * 
 * ============================================================
 */

#include "config.h"
#include "adc_capture.h"
#include "freq_detect.h"
#include "thd_measure.h"  // 已有的THD计算模块
#include <stdio.h>
#include <string.h>
#include <math.h>

/* -------- 外部模块声明 -------- */
extern void OLED_Init(void);
extern void OLED_Clear(void);
extern void OLED_ShowString(uint8_t x, uint8_t y, const char* str);
extern void OLED_DrawWaveform(int16_t* data, uint16_t len, uint8_t y_start, uint8_t height);
extern void OLED_DrawBarChart(float* values, uint8_t count, uint8_t y_start, uint8_t height);

extern void BT_Init(void);
extern void BT_SendString(const char* str);

/* -------- 系统状态 -------- */
typedef enum {
    STATE_IDLE,         // 等待启动
    STATE_COARSE,       // 粗采样
    STATE_FREQ,         // 频率检测
    STATE_RANGE,        // 量程检查
    STATE_SYNC,         // 同步采样
    STATE_FFT,          // FFT处理
    STATE_THD,          // THD计算
    STATE_DISPLAY,      // 显示结果
    STATE_BT,           // 蓝牙发送
    STATE_DONE          // 完成
} MeasureState_t;

/* -------- 测量结果 -------- */
static float measured_freq = 0;
static float actual_sample_rate = 0;
static uint8_t current_range = RANGE_HIGH;
static THDResult_t thd_result = {0};

/* -------- 多次平均 -------- */
#define NUM_AVERAGES 5  // 5次测量取平均
static float thd_accumulator = 0;
static float harm_accumulator[6] = {0};

/* ============================================================
 * 量程控制
 * ============================================================ */

static void set_range(uint8_t range)
{
    current_range = range;
    if (range == RANGE_LOW) {
        // CD4051选择×10放大通道
        MAP_GPIO_setOutputHighOnPin(RANGE_SEL_PORT, RANGE_SEL_PIN);
    } else {
        // CD4051选择直通通道
        MAP_GPIO_setOutputLowOnPin(RANGE_SEL_PORT, RANGE_SEL_PIN);
    }
    // 等待模拟开关稳定
    __delay_cycles(48000);  // 1ms @ 48MHz
}

/**
 * 自动量程检测
 * 
 * 【策略】
 * 先用高量程采样，检查信号峰值。
 * 如果峰值太小（<AUTO_RANGE_THRESHOLD），说明信号<100mVpp，
 * 切换到低量程（×10放大）。
 * 
 * 【为什么不用AGC（自动增益控制）？】
 * AGC需要反馈环路，响应时间长（几十ms到几百ms）。
 * 量程切换只需要1ms，而且只有两档，简单可靠。
 * 电赛4天时间，简单可靠比花哨重要。
 */
static void auto_range_detect(uint16_t* raw_data, uint16_t len)
{
    // 找峰值（相对于中间值的偏移）
    int16_t max_deviation = 0;
    for (uint16_t i = 0; i < len; i++) {
        int16_t dev = abs((int16_t)raw_data[i] - ADC_MID_VALUE);
        if (dev > max_deviation) max_deviation = dev;
    }
    
    if (max_deviation < AUTO_RANGE_THRESHOLD && current_range == RANGE_HIGH) {
        set_range(RANGE_LOW);
    } else if (max_deviation > ADC_MID_VALUE * 0.8f && current_range == RANGE_LOW) {
        // 信号太大，低量程会饱和，切回高量程
        set_range(RANGE_HIGH);
    }
}

/* ============================================================
 * 蜂鸣器
 * ============================================================ */

static void buzzer_beep(uint16_t duration_ms)
{
    MAP_GPIO_setOutputHighOnPin(BUZZER_PORT, BUZZER_PIN);
    uint32_t cycles = (uint32_t)duration_ms * (SYSTEM_CLOCK_HZ / 1000);
    __delay_cycles(cycles);
    MAP_GPIO_setOutputLowOnPin(BUZZER_PORT, BUZZER_PIN);
}

/* ============================================================
 * 主程序
 * ============================================================ */

int main(void)
{
    /* ---- 系统初始化 ---- */
    MAP_WDT_A_holdTimer();  // 关看门狗
    
    // 时钟配置：MCLK=48MHz, SMCLK=24MHz
    MAP_CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_48);
    MAP_CS_initClockSignal(CS_MCLK, CS_DCOCLK_SELECT, CS_CLOCK_DIVIDER_1);
    MAP_CS_initClockSignal(CS_SMCLK, CS_DCOCLK_SELECT, CS_CLOCK_DIVIDER_2);
    
    // GPIO初始化
    MAP_GPIO_setAsOutputPin(LED_BUSY_PORT, LED_BUSY_PIN);
    MAP_GPIO_setAsOutputPin(LED_DONE_PORT, LED_DONE_PIN);
    MAP_GPIO_setAsOutputPin(BUZZER_PORT, BUZZER_PIN);
    MAP_GPIO_setAsOutputPin(RANGE_SEL_PORT, RANGE_SEL_PIN);
    MAP_GPIO_setAsInputPinWithPullUpResistor(BTN_START_PORT, BTN_START_PIN);
    
    // 模块初始化
    ADC_Capture_Init();
    thd_init();
    init_flat_top_window();
    OLED_Init();
    BT_Init();
    
    // 初始量程
    set_range(RANGE_HIGH);
    
    // 使能全局中断
    MAP_Interrupt_enableMaster();
    
    // 显示待机画面
    OLED_Clear();
    OLED_ShowString(0, 0, "THD Meter v1.0");
    OLED_ShowString(0, 2, "TI MSP432");
    OLED_ShowString(0, 4, "Press S1 to");
    OLED_ShowString(0, 6, "start measure");
    
    MAP_GPIO_setOutputLowOnPin(LED_BUSY_PORT, LED_BUSY_PIN);
    MAP_GPIO_setOutputHighOnPin(LED_DONE_PORT, LED_DONE_PIN);  // 绿灯=就绪
    
    /* ============================================================
     * 主循环
     * ============================================================ */
    while (1)
    {
        // 等待启动按键
        while (MAP_GPIO_getInputPinValue(BTN_START_PORT, BTN_START_PIN) != 0) {
            __delay_cycles(480000);  // 10ms消抖
        }
        // 等待按键释放
        while (MAP_GPIO_getInputPinValue(BTN_START_PORT, BTN_START_PIN) == 0);
        __delay_cycles(4800000);  // 100ms消抖
        
        // ===== 开始测量 =====
        MAP_GPIO_setOutputHighOnPin(LED_BUSY_PORT, LED_BUSY_PIN);   // 红灯=忙
        MAP_GPIO_setOutputLowOnPin(LED_DONE_PORT, LED_DONE_PIN);
        
        OLED_Clear();
        OLED_ShowString(0, 0, "Measuring...");
        
        // ---- Step 1: 粗采样 ----
        ADC_Capture_SetRate(INITIAL_SAMPLE_RATE);
        ADC_Capture_Start(FFT_SIZE);
        while (!ADC_Capture_IsComplete());
        
        // ---- Step 2: 频率检测 ----
        int16_t* signed_data;
        uint16_t data_len;
        ADC_Capture_GetSignedData(&signed_data, &data_len);
        
        measured_freq = FreqDetect_Measure(signed_data, data_len, INITIAL_SAMPLE_RATE);
        
        if (measured_freq < 50.0f || measured_freq > 200000.0f) {
            // 频率检测失败或超出范围
            OLED_ShowString(0, 2, "No signal!");
            buzzer_beep(500);
            MAP_GPIO_setOutputLowOnPin(LED_BUSY_PORT, LED_BUSY_PIN);
            MAP_GPIO_setOutputHighOnPin(LED_DONE_PORT, LED_DONE_PIN);
            continue;
        }
        
        // ---- Step 3: 量程检查 ----
        uint16_t* raw_data;
        ADC_Capture_GetData(&raw_data, &data_len);
        auto_range_detect(raw_data, data_len);
        
        // ---- Step 4: 计算同步采样率 ----
        float sync_rate = FreqDetect_CalcSyncRate(measured_freq, INITIAL_SAMPLE_RATE);
        actual_sample_rate = ADC_Capture_SetRate(sync_rate);
        
        // ---- Step 5~7: 多次采样取平均 ----
        thd_accumulator = 0;
        memset(harm_accumulator, 0, sizeof(harm_accumulator));
        
        float v_scale = (current_range == RANGE_HIGH) ? V_SCALE_HIGH : V_SCALE_LOW;
        
        for (int avg = 0; avg < NUM_AVERAGES; avg++) {
            // 采集
            ADC_Capture_Start(FFT_SIZE);
            while (!ADC_Capture_IsComplete());
            
            ADC_Capture_GetData(&raw_data, &data_len);
            
            // FFT + THD计算
            thd_result = measure_thd(raw_data, data_len, actual_sample_rate,
                                      v_scale, (float)ADC_MID_VALUE);
            
            // 累加
            thd_accumulator += thd_result.thd_percent;
            for (int h = 1; h <= 5; h++) {
                harm_accumulator[h] += thd_result.harmonic_norm[h];
            }
        }
        
        // 取平均
        thd_result.thd_percent = thd_accumulator / NUM_AVERAGES;
        for (int h = 1; h <= 5; h++) {
            thd_result.harmonic_norm[h] = harm_accumulator[h] / NUM_AVERAGES;
        }
        thd_result.frequency = measured_freq;
        
        // ---- Step 8: OLED显示 ----
        OLED_Clear();
        
        char buf[22];
        
        // 第0行：THD值（最重要的结果，放最上面）
        snprintf(buf, sizeof(buf), "THD=%.1f%%", thd_result.thd_percent);
        OLED_ShowString(0, 0, buf);
        
        // 第1行：频率
        if (measured_freq < 1000) {
            snprintf(buf, sizeof(buf), "f=%.1fHz", measured_freq);
        } else {
            snprintf(buf, sizeof(buf), "f=%.2fkHz", measured_freq / 1000.0f);
        }
        OLED_ShowString(0, 1, buf);
        
        // 第2行：基波有效值
        snprintf(buf, sizeof(buf), "V1=%.1fmV", thd_result.fundamental_vrms * 1000);
        OLED_ShowString(0, 2, buf);
        
        // 第3~4行：归一化谐波幅值（柱状图或数字）
        snprintf(buf, sizeof(buf), "H2=%.2f H3=%.2f", 
                 thd_result.harmonic_norm[2], thd_result.harmonic_norm[3]);
        OLED_ShowString(0, 3, buf);
        
        snprintf(buf, sizeof(buf), "H4=%.2f H5=%.2f",
                 thd_result.harmonic_norm[4], thd_result.harmonic_norm[5]);
        OLED_ShowString(0, 4, buf);
        
        // 第5~7行：波形显示（发挥部分）
        ADC_Capture_GetSignedData(&signed_data, &data_len);
        // 显示一个周期的波形
        uint16_t samples_per_cycle = (uint16_t)(actual_sample_rate / measured_freq);
        if (samples_per_cycle > data_len) samples_per_cycle = data_len;
        OLED_DrawWaveform(signed_data, samples_per_cycle, 40, 24);
        
        // ---- Step 9: 蓝牙发送 ----
        snprintf(buf, sizeof(buf), "THD=%.2f%%\r\n", thd_result.thd_percent);
        BT_SendString(buf);
        snprintf(buf, sizeof(buf), "f=%.1fHz\r\n", measured_freq);
        BT_SendString(buf);
        for (int h = 1; h <= 5; h++) {
            snprintf(buf, sizeof(buf), "H%d=%.4f\r\n", h, thd_result.harmonic_norm[h]);
            BT_SendString(buf);
        }
        
        // ---- Step 10: 完成 ----
        buzzer_beep(200);  // 短响一声
        MAP_GPIO_setOutputLowOnPin(LED_BUSY_PORT, LED_BUSY_PIN);
        MAP_GPIO_setOutputHighOnPin(LED_DONE_PORT, LED_DONE_PIN);  // 绿灯=完成
        
        // 结果保持显示，等待下一次按键
    }
}
