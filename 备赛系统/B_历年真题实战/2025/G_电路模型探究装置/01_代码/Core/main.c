/**
 * 2025G 电路模型探究装置 —— 主程序
 * MCU: STM32F407VET6 @168MHz
 * 
 * ============================================================
 * 【系统状态机】
 * ============================================================
 * 
 * IDLE → (按学习键) → LEARNING → (学习完成) → READY
 * READY → (按启动键) → RUNNING → (按停止键) → IDLE
 * 
 * IDLE:     等待操作，显示"就绪"
 * LEARNING: 扫频测量未知电路，显示进度
 * READY:    学习完成，显示滤波器类型，等待启动
 * RUNNING:  实时采集→处理→输出，显示工作状态
 * 
 * ============================================================
 * 【推理阶段的实时处理流程】
 * ============================================================
 * 
 * 这是整个系统最关键的部分。
 * 
 * ADC以200kHz连续采样输入信号（来自外部信号发生器）。
 * 每采集FFT_SIZE(2048)个点，触发一次处理：
 *   1. 将ADC数据复制到处理缓冲区
 *   2. FFT → 查表 → IFFT（约7ms）
 *   3. 将结果写入DAC输出缓冲区
 * 
 * DAC以200kHz连续输出（DMA循环模式，双缓冲）。
 * 
 * 时序：
 *   采集一帧：2048/200000 = 10.24ms
 *   处理一帧：约7ms
 *   处理时间 < 采集时间 → 来得及！
 * 
 * 延迟：一帧 = 10.24ms
 * 对于1kHz信号，10.24ms ≈ 10个周期的延迟
 * 对于50Hz信号，10.24ms ≈ 0.5个周期的延迟
 * 题目没有延迟要求，只要求"同频稳定显示"，所以延迟不是问题。
 * 
 * ============================================================
 */

#include "config.h"
#include "../Drivers/dds_ad9833.h"
#include "../Drivers/adc_dual_sync.h"
#include "../Drivers/dac_output.h"
#include "../Algorithm/freq_response.h"
#include "../Algorithm/signal_processor.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * FreqResp 回调适配器（算法层 ←→ 硬件层 解耦）
 * ============================================================ */

static void freqresp_dds_set(float freq_hz)
{
    DDS_SetFreq(freq_hz);
    DDS_SetWaveform(DDS_WAVE_SINE);
    DDS_Start();
}

static void freqresp_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static uint8_t freqresp_capture(float freq_hz,
                                const int16_t **in_data,
                                const int16_t **out_data,
                                float *sample_rate,
                                uint16_t *length)
{
    /* 设置采样率：让 10 个周期落在 N 点内 */
    float ideal_rate = freq_hz * (float)DFT_SAMPLES_PER_POINT / 10.0f;
    if (ideal_rate < 10000.0f)   ideal_rate = 10000.0f;
    if (ideal_rate > 1000000.0f) ideal_rate = 1000000.0f;
    *sample_rate = ADC_DualSync_SetRate(ideal_rate);

    ADC_DualSync_StartCapture(DFT_SAMPLES_PER_POINT);
    while (!ADC_DualSync_IsComplete()) { /* busy wait */ }

    int16_t *ch1; int16_t *ch2; uint16_t len;
    ADC_DualSync_GetData(&ch1, &ch2, &len);
    *in_data = ch1;
    *out_data = ch2;
    *length = len;
    return 1;
}

static const FreqResp_Callbacks_t s_freqresp_cb = {
    .dds_set_freq = freqresp_dds_set,
    .delay_ms     = freqresp_delay_ms,
    .capture      = freqresp_capture,
};

/* -------- 外设句柄（CubeMX生成） -------- */
extern SPI_HandleTypeDef hspi1;
extern ADC_HandleTypeDef hadc1, hadc2;
extern DAC_HandleTypeDef hdac;
extern TIM_HandleTypeDef htim2, htim6;
extern I2C_HandleTypeDef hi2c1;

/* -------- 系统状态 -------- */
typedef enum {
    STATE_IDLE,
    STATE_LEARNING,
    STATE_READY,
    STATE_RUNNING
} SystemState_t;

static volatile SystemState_t sys_state = STATE_IDLE;

/* -------- 推理阶段的缓冲区 -------- */
// ADC采集缓冲区（推理阶段用，连续采集）
static int16_t inference_adc_buf[FFT_SIZE];
static volatile uint8_t inference_frame_ready = 0;

// 处理结果缓冲区
static uint16_t inference_dac_buf[FFT_SIZE];

/* -------- 简易OLED显示（接口声明） -------- */
extern void OLED_Init(I2C_HandleTypeDef* hi2c);
extern void OLED_Clear(void);
extern void OLED_ShowString(uint8_t x, uint8_t y, const char* str);

/* -------- 按键读取（消抖） -------- */
static uint8_t btn_pressed(GPIO_TypeDef* port, uint16_t pin)
{
    if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) {
        HAL_Delay(50);  // 消抖
        if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) {
            while (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET);  // 等待释放
            return 1;
        }
    }
    return 0;
}

/* -------- 滤波器类型转字符串 -------- */
static const char* filter_type_str(FilterType_t type)
{
    switch (type) {
        case FILT_LOWPASS:  return "Low-Pass";
        case FILT_HIGHPASS: return "High-Pass";
        case FILT_BANDPASS: return "Band-Pass";
        case FILT_BANDSTOP: return "Band-Stop";
        default:            return "Unknown";
    }
}

/* ============================================================
 * 主程序
 * ============================================================ */

int main(void)
{
    /* -------- 系统初始化 -------- */
    HAL_Init();
    SystemClock_Config();  // 168MHz（CubeMX生成）
    
    // 初始化所有外设（CubeMX生成的MX_xxx_Init函数）
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_ADC1_Init();
    MX_ADC2_Init();
    MX_DAC_Init();
    MX_TIM2_Init();
    MX_TIM6_Init();
    MX_I2C1_Init();
    
    // 初始化自定义模块
    DDS_Init(&hspi1);
    ADC_DualSync_Init();
    DAC_Out_Init();
    SigProc_Init();
    OLED_Init(&hi2c1);

    // 注入算法层回调（FreqResp_Learn 内部用）
    FreqResp_BindCallbacks(&s_freqresp_cb);
    
    // 显示启动信息
    OLED_Clear();
    OLED_ShowString(0, 0, "2025G Circuit");
    OLED_ShowString(0, 2, "Model Explorer");
    OLED_ShowString(0, 4, "Status: IDLE");
    OLED_ShowString(0, 6, "Press LEARN");
    
    sys_state = STATE_IDLE;
    
    /* ============================================================
     * 主循环
     * ============================================================ */
    while (1)
    {
        switch (sys_state)
        {
        /* -------- 空闲状态 -------- */
        case STATE_IDLE:
            HAL_GPIO_WritePin(LED_LEARN_PORT, LED_LEARN_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_RESET);
            
            if (btn_pressed(BTN_LEARN_PORT, BTN_LEARN_PIN)) {
                sys_state = STATE_LEARNING;
                OLED_Clear();
                OLED_ShowString(0, 0, "LEARNING...");
                OLED_ShowString(0, 2, "Sweeping freq");
                HAL_GPIO_WritePin(LED_LEARN_PORT, LED_LEARN_PIN, GPIO_PIN_SET);
            }
            break;
            
        /* -------- 学习状态 -------- */
        case STATE_LEARNING:
        {
            /*
             * 调用FreqResp_Learn()执行完整的扫频学习。
             * 这个函数内部会：
             *   1. 粗扫20个频率点
             *   2. 识别滤波器类型
             *   3. 在截止频率附近细扫30个点
             *   4. 排序+相位解缠绕
             * 总耗时约10~30秒（远小于2分钟限制）
             */
            uint16_t num_points = FreqResp_Learn();
            FilterType_t ftype = FreqResp_GetFilterType();
            
            // 显示结果
            OLED_Clear();
            OLED_ShowString(0, 0, "Learn Complete!");
            
            char buf[22];
            snprintf(buf, sizeof(buf), "Points: %d", num_points);
            OLED_ShowString(0, 2, buf);
            
            snprintf(buf, sizeof(buf), "Type: %s", filter_type_str(ftype));
            OLED_ShowString(0, 4, buf);
            
            OLED_ShowString(0, 6, "Press START");
            
            HAL_GPIO_WritePin(LED_LEARN_PORT, LED_LEARN_PIN, GPIO_PIN_RESET);
            sys_state = STATE_READY;
            break;
        }
            
        /* -------- 就绪状态（等待启动） -------- */
        case STATE_READY:
            if (btn_pressed(BTN_START_PORT, BTN_START_PIN)) {
                /*
                 * 切换PA4从SPI1_NSS(DDS FSYNC)到DAC输出模式。
                 * 
                 * 【这是引脚复用的关键时刻】
                 * 学习阶段PA4是DDS的FSYNC（GPIO输出模式）
                 * 推理阶段PA4是DAC1_OUT1（模拟输出模式）
                 * 需要重新配置GPIO模式。
                 * 
                 * 如果用了SPI2给DDS（备选方案），这里就不需要切换。
                 */
                GPIO_InitTypeDef gpio = {0};
                gpio.Pin = DAC_OUT_PIN;
                gpio.Mode = GPIO_MODE_ANALOG;
                gpio.Pull = GPIO_NOPULL;
                HAL_GPIO_Init(DAC_OUT_PORT, &gpio);
                
                // 配置ADC和DAC的采样/输出率
                float actual_rate = ADC_DualSync_SetRate(INFERENCE_SAMPLE_RATE);
                DAC_Out_SetRate(actual_rate);  // DAC和ADC使用相同的速率
                
                // 启动DAC连续输出
                DAC_Out_Start();
                
                // 启动ADC连续采集（推理模式）
                // 这里需要重新配置ADC为连续模式
                // （学习阶段是单次采集模式）
                ADC_DualSync_StartCapture(FFT_SIZE);
                
                OLED_Clear();
                OLED_ShowString(0, 0, "RUNNING");
                OLED_ShowString(0, 2, "Processing...");
                
                HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_SET);
                sys_state = STATE_RUNNING;
            }
            break;
            
        /* -------- 运行状态（实时处理） -------- */
        case STATE_RUNNING:
        {
            /*
             * 推理主循环：
             * 1. 等待ADC采集完一帧
             * 2. 获取ADC数据
             * 3. FFT → 查表 → IFFT
             * 4. 将结果写入DAC缓冲区
             * 5. 重新启动ADC采集下一帧
             */
            
            if (ADC_DualSync_IsComplete()) {
                // 获取ADC数据（只需要输入通道ch1，ch2在推理阶段不用）
                int16_t *ch1, *ch2;
                uint16_t len;
                ADC_DualSync_GetData(&ch1, &ch2, &len);
                
                // 复制到处理缓冲区（避免DMA覆盖）
                memcpy(inference_adc_buf, ch1, len * sizeof(int16_t));
                
                // 重新启动ADC采集（采集下一帧的同时处理当前帧）
                ADC_DualSync_StartCapture(FFT_SIZE);
                
                // 信号处理：FFT → 查表 → IFFT
                SigProc_ProcessFrame(inference_adc_buf, inference_dac_buf, 
                                      FFT_SIZE, INFERENCE_SAMPLE_RATE);
                
                // 将结果写入DAC输出缓冲区
                uint16_t* dac_write_ptr;
                uint16_t dac_write_len;
                if (DAC_Out_GetWriteBuffer(&dac_write_ptr, &dac_write_len)) {
                    memcpy(dac_write_ptr, inference_dac_buf, 
                           dac_write_len * sizeof(uint16_t));
                    DAC_Out_WriteComplete();
                }
                // 如果没有可写入的缓冲区，说明处理太慢了（不应该发生）
            }
            
            // 检查停止键
            if (btn_pressed(BTN_STOP_PORT, BTN_STOP_PIN)) {
                DAC_Out_Stop();
                HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_RESET);
                
                OLED_Clear();
                OLED_ShowString(0, 0, "STOPPED");
                OLED_ShowString(0, 4, "Press LEARN");
                
                sys_state = STATE_IDLE;
            }
            break;
        }
        
        default:
            sys_state = STATE_IDLE;
            break;
        }
        
        // 主循环不加延时——推理阶段需要尽快处理每一帧
        // 空闲和就绪状态下CPU会在btn_pressed()的消抖延时中等待
    }
}
