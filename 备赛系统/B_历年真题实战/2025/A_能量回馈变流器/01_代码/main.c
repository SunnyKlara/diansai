/**
 * 2025A 能量回馈的变流器负载试验装置 - 主程序
 * MCU: STM32G474RET6
 * 
 * 功能：
 *   - 三相SVPWM逆变输出 32V/50Hz (可调20~100Hz)
 *   - 三相电压闭环稳压
 *   - 按键设置频率
 *   - OLED显示输出参数
 *   - 过压过流保护
 * 
 * 引脚分配：
 *   PA8/PA7   TIM1_CH1/CH1N  A相桥臂
 *   PA9/PB0   TIM1_CH2/CH2N  B相桥臂
 *   PA10/PB1  TIM1_CH3/CH3N  C相桥臂
 *   PA0~PA2   ADC1_CH1~3     三相电压采样
 *   PA3~PA5   ADC2_CH1~3     三相电流采样
 *   PB6/PB7   I2C1           OLED
 *   PC0~PC3   GPIO           按键(频率+/-、启动、停止)
 */

#include "stm32g4xx_hal.h"
#include "svpwm_3phase.h"
#include "arm_math.h"

/* ========== 系统参数 ========== */
#define VOUT_LINE_REF   32.0f    // 输出线电压有效值设定 (V)
#define FREQ_DEFAULT    50.0f    // 默认输出频率 (Hz)
#define FREQ_MIN        20.0f
#define FREQ_MAX        100.0f

/* ========== 全局变量 ========== */
volatile float g_vout_line_rms = 0;  // 输出线电压有效值
volatile float g_iout_rms = 0;       // 输出电流有效值
volatile float g_freq_set = FREQ_DEFAULT;
volatile float g_mod_index = 0.8f;
volatile uint8_t g_running = 0;

/* ========== 电压闭环 ========== */
static float v_integral = 0;
#define KP_V  0.015f
#define KI_V  0.003f

float voltage_loop(float vout_rms)
{
    float error = VOUT_LINE_REF - vout_rms;
    
    v_integral += KI_V * error;
    if (v_integral > 0.95f) v_integral = 0.95f;
    if (v_integral < 0.1f) v_integral = 0.1f;
    
    float mod = KP_V * error + v_integral;
    if (mod > 0.95f) mod = 0.95f;
    if (mod < 0.1f) mod = 0.1f;
    
    return mod;
}

/* ========== 三相电压RMS计算 ========== */
#define SAMPLES_PER_CYCLE  400  // 20kHz/50Hz
static uint16_t adc_buf_va[SAMPLES_PER_CYCLE];
static uint16_t adc_buf_vb[SAMPLES_PER_CYCLE];
static uint16_t sample_idx = 0;

// 在ADC中断中采集数据
void collect_voltage_sample(uint16_t va, uint16_t vb)
{
    adc_buf_va[sample_idx] = va;
    adc_buf_vb[sample_idx] = vb;
    sample_idx++;
    
    if (sample_idx >= SAMPLES_PER_CYCLE) {
        sample_idx = 0;
        
        // 计算线电压Vab的RMS
        float sum_sq = 0;
        for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
            float va_v = (float)adc_buf_va[i] / 4096.0f * 3.3f * 11.0f; // 分压比1:11
            float vb_v = (float)adc_buf_vb[i] / 4096.0f * 3.3f * 11.0f;
            float vab = va_v - vb_v;  // 线电压 = 相电压差
            sum_sq += vab * vab;
        }
        g_vout_line_rms = sqrtf(sum_sq / SAMPLES_PER_CYCLE);
        
        // 执行电压闭环
        g_mod_index = voltage_loop(g_vout_line_rms);
        SVPWM_SetAmplitude(g_mod_index);
    }
}

/* ========== 按键处理 ========== */
void check_buttons(void)
{
    // 频率+
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        g_freq_set += 1.0f;
        if (g_freq_set > FREQ_MAX) g_freq_set = FREQ_MAX;
        SVPWM_SetFrequency(g_freq_set);
    }
    
    // 频率-
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        g_freq_set -= 1.0f;
        if (g_freq_set < FREQ_MIN) g_freq_set = FREQ_MIN;
        SVPWM_SetFrequency(g_freq_set);
    }
    
    // 启动
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        if (!g_running) {
            g_running = 1;
            // 启动TIM1 PWM输出
            HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
            HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
            HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
            HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
            HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
            HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
        }
    }
    
    // 停止
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        g_running = 0;
        HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
        HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
        HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
        HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
        HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
        HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
    }
}

/* ========== TIM1更新中断 ========== */
extern TIM_HandleTypeDef htim1;

void TIM1_UP_TIM16_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        
        if (g_running) {
            Inverter3Ph_Update();
        }
    }
}

/* ========== 主程序 ========== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();  // 170MHz
    
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM1_Init();   // 三相PWM, 20kHz, 死区800ns
    MX_ADC1_Init();    // 电压采样
    MX_ADC2_Init();    // 电流采样
    MX_I2C1_Init();    // OLED
    
    SVPWM_Init();
    SVPWM_SetFrequency(g_freq_set);
    SVPWM_SetAmplitude(g_mod_index);
    
    // 显示初始信息
    // Display_ShowString(0, 0, "2025A 3Ph Inv");
    // Display_ShowString(0, 2, "Freq: 50Hz");
    // Display_ShowString(0, 4, "Status: READY");
    
    while (1)
    {
        check_buttons();
        
        // 每200ms更新显示
        static uint32_t last_disp = 0;
        if (HAL_GetTick() - last_disp >= 200) {
            last_disp = HAL_GetTick();
            
            // Display_ShowFloat(0, 2, "Vab:", g_vout_line_rms, "V");
            // Display_ShowFloat(0, 4, "Freq:", g_freq_set, "Hz");
            // Display_ShowFloat(0, 6, "Mod:", g_mod_index, "");
        }
    }
}
