/**
 * 2023A 单相逆变器并联运行系统 - 主程序
 * MCU: STM32G474RET6
 * 功能: SPWM逆变 + 电压闭环 + LCD显示
 * 
 * 硬件连接:
 *   PA8/PA9  -> TIM1_CH1/CH2 (全桥逆变PWM, 互补输出)
 *   PA0      -> ADC1_CH1 (输出电压采样)
 *   PA1      -> ADC1_CH2 (输出电流采样)
 *   PB6/PB7  -> I2C1 (OLED显示)
 */

#include "stm32g4xx_hal.h"
#include "spwm.h"
#include "control.h"
#include "adc_sample.h"
#include "display.h"
#include "protection.h"

/* ========== 系统参数 ========== */
#define VOUT_REF        24.0f    // 输出电压设定值 (Vrms)
#define FREQ_REF        50.0f    // 输出频率 (Hz)
#define FSW             20000    // 开关频率 (Hz)
#define SINE_TABLE_LEN  (FSW / (int)FREQ_REF)  // 正弦表长度 = 400

/* ========== 全局变量 ========== */
volatile float g_vout_rms = 0;       // 输出电压有效值
volatile float g_iout_rms = 0;       // 输出电流有效值
volatile float g_mod_index = 0.7f;   // 调制比 (初始值)
volatile uint8_t g_system_running = 0;

/* ========== 外设句柄 ========== */
TIM_HandleTypeDef htim1;    // PWM定时器
ADC_HandleTypeDef hadc1;    // ADC
DMA_HandleTypeDef hdma_adc1;
I2C_HandleTypeDef hi2c1;    // OLED

/* ========== 函数声明 ========== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();  // 170MHz
    
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM1_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    
    // 初始化各模块
    SPWM_Init(SINE_TABLE_LEN);
    Control_Init(VOUT_REF);
    ADC_Sample_Init(&hadc1, &hdma_adc1);
    Display_Init(&hi2c1);
    Protection_Init();
    
    // 显示启动信息
    Display_ShowString(0, 0, "2023A Inverter");
    Display_ShowString(0, 2, "Vref: 24.0V");
    Display_ShowString(0, 4, "Status: READY");
    
    // 等待启动按键
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        HAL_Delay(10);
    }
    HAL_Delay(200);  // 消抖
    
    // 启动PWM输出
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    
    // 启动ADC DMA采样
    ADC_Sample_Start();
    
    g_system_running = 1;
    Display_ShowString(0, 4, "Status: RUN   ");
    
    /* 主循环 */
    while (1)
    {
        // 每100ms更新显示
        static uint32_t last_display = 0;
        if (HAL_GetTick() - last_display >= 100) {
            last_display = HAL_GetTick();
            
            Display_ShowFloat(0, 2, "Vout:", g_vout_rms, "V");
            Display_ShowFloat(0, 4, "Iout:", g_iout_rms, "A");
            Display_ShowFloat(0, 6, "Mod: ", g_mod_index, "");
        }
        
        // 保护检测
        if (Protection_Check(g_vout_rms, g_iout_rms)) {
            // 过压/过流，停机
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
            HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
            HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
            g_system_running = 0;
            Display_ShowString(0, 4, "FAULT!        ");
            while(1);  // 停机等待复位
        }
    }
}

/**
 * TIM1更新中断 - 每个开关周期执行一次 (20kHz)
 * 这是整个系统的核心：更新SPWM占空比
 */
void TIM1_UP_TIM16_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        
        if (!g_system_running) return;
        
        // 获取当前正弦表值并更新PWM
        float sine_val = SPWM_GetNextValue();
        
        // 计算占空比 (单极性SPWM)
        float duty_a = 0.5f + 0.5f * g_mod_index * sine_val;
        float duty_b = 0.5f - 0.5f * g_mod_index * sine_val;
        
        // 更新CCR寄存器
        uint16_t arr = htim1.Instance->ARR;
        htim1.Instance->CCR1 = (uint16_t)(duty_a * arr);
        htim1.Instance->CCR2 = (uint16_t)(duty_b * arr);
    }
}

/**
 * ADC DMA传输完成中断 - 一个正弦周期的数据采集完成
 * 在这里计算RMS值并执行电压闭环
 */
void DMA1_Channel1_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF1) {
        DMA1->IFCR = DMA_IFCR_CTCIF1;
        
        // 计算输出电压和电流的RMS值
        g_vout_rms = ADC_Sample_CalcVrms();
        g_iout_rms = ADC_Sample_CalcIrms();
        
        // 电压闭环控制 - 调节调制比
        g_mod_index = Control_VoltageLoop(g_vout_rms);
    }
}
