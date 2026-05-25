/**
 * 2025F 简易自动接收机 - 主程序
 * MCU: STM32F103C8T6
 * 
 * 功能：
 *   - 自动扫描88~108MHz，搜索FM/AM信号
 *   - 自动识别调制方式(CW/FM/AM)
 *   - 解调输出到8Ω负载
 *   - AGC控制输出1V±0.1V
 *   - OLED显示频率和调制方式
 * 
 * 硬件架构：
 *   天线(SMA) → LNA(SPF5189Z) → 混频器(SA612) → IF滤波(10.7MHz陶瓷)
 *                                    ↑
 *                              本振PLL(Si5351)
 *   → IF放大(AGC) → FM鉴频/AM检波 → 音频放大(LM386) → 8Ω负载
 *   → RSSI检测 → STM32(扫描/识别/AGC/显示)
 * 
 * 引脚：
 *   PB6/PB7  I2C1  Si5351 + OLED
 *   PA0      ADC   RSSI检测(对数检波器输出)
 *   PA1      ADC   AGC反馈(输出幅度检测)
 *   PA2      ADC   包络检测(AM/FM识别用)
 *   PA3      ADC   鉴频器输出检测
 *   PA8      GPIO  FM/AM解调切换(模拟开关控制)
 *   PB0      GPIO  AGC控制电压(PWM→RC滤波→VGA控制)
 *   PC13     GPIO  启动按键
 */

#include "stm32f1xx_hal.h"
#include "si5351.h"
#include <stdio.h>
#include <math.h>

/* ========== 参数 ========== */
#define FREQ_START      88000000UL    // 88MHz
#define FREQ_STOP       108000000UL   // 108MHz
#define FREQ_STEP       100000UL      // 100kHz步进
#define IF_FREQ         10700000UL    // 10.7MHz中频
#define RSSI_THRESHOLD  1.0f          // RSSI门限(V)，需要实际标定
#define AGC_TARGET      1.0f          // AGC目标输出峰峰值(V)

/* ========== 调制方式 ========== */
typedef enum {
    MOD_NONE,
    MOD_CW,
    MOD_FM,
    MOD_AM
} ModType_t;

/* ========== 全局变量 ========== */
volatile uint32_t current_freq = 0;
volatile ModType_t current_mod = MOD_NONE;
volatile float current_rssi = 0;
volatile float output_level = 0;

/* ========== Si5351本振控制 ========== */
// Si5351通过I2C控制，输出频率 = 信号频率 + IF
// 例：接收100MHz → 本振=100+10.7=110.7MHz

void set_lo_freq(uint32_t signal_freq)
{
    uint32_t lo_freq = signal_freq + IF_FREQ;
    Si5351_SetFreq(0, lo_freq);  // CLK0输出
}

/* ========== RSSI读取 ========== */
float read_rssi(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t val = HAL_ADC_GetValue(&hadc1);
    return (float)val / 4096.0f * 3.3f;
}

/* ========== 调制方式识别 ========== */
ModType_t identify_modulation(void)
{
    // 采集包络信号和鉴频器输出各100个点
    float envelope[100], demod_fm[100];
    
    for (int i = 0; i < 100; i++) {
        // PA2: 包络检波输出
        ADC1->SQR3 = 2;  // CH2
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 5);
        envelope[i] = (float)HAL_ADC_GetValue(&hadc1) / 4096.0f * 3.3f;
        
        // PA3: 鉴频器输出
        ADC1->SQR3 = 3;  // CH3
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 5);
        demod_fm[i] = (float)HAL_ADC_GetValue(&hadc1) / 4096.0f * 3.3f;
        
        HAL_Delay(1);  // 1ms间隔，100点=100ms≈5个50Hz周期
    }
    
    // 计算包络方差
    float env_mean = 0, env_var = 0;
    for (int i = 0; i < 100; i++) env_mean += envelope[i];
    env_mean /= 100;
    for (int i = 0; i < 100; i++) {
        float d = envelope[i] - env_mean;
        env_var += d * d;
    }
    env_var /= 100;
    
    // 计算鉴频器输出方差
    float fm_mean = 0, fm_var = 0;
    for (int i = 0; i < 100; i++) fm_mean += demod_fm[i];
    fm_mean /= 100;
    for (int i = 0; i < 100; i++) {
        float d = demod_fm[i] - fm_mean;
        fm_var += d * d;
    }
    fm_var /= 100;
    
    // 判决
    if (env_var > 0.01f) {
        return MOD_AM;   // 包络有明显变化 → AM
    } else if (fm_var > 0.005f) {
        return MOD_FM;   // 鉴频器有输出 → FM
    } else {
        return MOD_CW;   // 都没有 → CW(连续载波)
    }
}

/* ========== AGC控制 ========== */
// 通过PWM输出→RC滤波→VGA(AD603)控制电压
// 控制输出幅度稳定在1V±0.1V

static float agc_integral = 0.5f;

void agc_update(void)
{
    // 读取输出幅度(PA1: 峰值检波器输出)
    ADC1->SQR3 = 1;
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 5);
    output_level = (float)HAL_ADC_GetValue(&hadc1) / 4096.0f * 3.3f;
    
    // PI控制
    float error = AGC_TARGET - output_level;
    agc_integral += 0.01f * error;
    if (agc_integral > 0.95f) agc_integral = 0.95f;
    if (agc_integral < 0.05f) agc_integral = 0.05f;
    
    float duty = 0.1f * error + agc_integral;
    if (duty > 0.95f) duty = 0.95f;
    if (duty < 0.05f) duty = 0.05f;
    
    // 输出PWM控制VGA增益
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, (uint16_t)(duty * 999));
}

/* ========== 解调切换 ========== */
void switch_demod(ModType_t mod)
{
    if (mod == MOD_AM) {
        // PA8=0: 选择AM检波器输出
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    } else {
        // PA8=1: 选择FM鉴频器输出
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    }
}

/* ========== 自动搜索 ========== */
void auto_search(void)
{
    // OLED显示搜索中
    OLED_ShowString(0, 0, "Searching...");
    
    for (uint32_t freq = FREQ_START; freq <= FREQ_STOP; freq += FREQ_STEP) {
        set_lo_freq(freq);
        HAL_Delay(5);  // 等PLL锁定
        
        current_rssi = read_rssi();
        
        // 显示当前扫描频率
        char buf[20];
        snprintf(buf, sizeof(buf), "%3lu.%1luMHz", freq/1000000, (freq%1000000)/100000);
        OLED_ShowString(0, 2, buf);
        
        if (current_rssi > RSSI_THRESHOLD) {
            // 找到信号
            current_freq = freq;
            
            // 识别调制方式
            current_mod = identify_modulation();
            
            // 切换解调器
            switch_demod(current_mod);
            
            // 显示结果
            OLED_Clear();
            snprintf(buf, sizeof(buf), "F:%3lu.%1luMHz", freq/1000000, (freq%1000000)/100000);
            OLED_ShowString(0, 0, buf);
            
            switch (current_mod) {
                case MOD_FM: OLED_ShowString(0, 2, "Mod: FM"); break;
                case MOD_AM: OLED_ShowString(0, 2, "Mod: AM"); break;
                case MOD_CW: OLED_ShowString(0, 2, "Mod: CW"); break;
                default:     OLED_ShowString(0, 2, "Mod: ???"); break;
            }
            
            OLED_ShowString(0, 4, "Demodulating...");
            return;
        }
    }
    
    OLED_ShowString(0, 4, "No signal found");
}

/* ========== 主程序 ========== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    
    MX_GPIO_Init();
    MX_I2C1_Init();    // Si5351 + OLED
    MX_ADC1_Init();    // RSSI + AGC + 识别
    MX_TIM3_Init();    // AGC PWM输出
    
    OLED_Init();
    Si5351_Init();
    
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);  // AGC PWM
    
    OLED_ShowString(0, 0, "2025F Receiver");
    OLED_ShowString(0, 2, "88-108MHz");
    OLED_ShowString(0, 4, "Press to start");
    
    // 等待启动按键
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        HAL_Delay(10);
    }
    HAL_Delay(200);
    
    // 自动搜索
    auto_search();
    
    // 主循环：AGC控制 + 显示更新
    while (1) {
        agc_update();
        
        static uint32_t last_disp = 0;
        if (HAL_GetTick() - last_disp >= 200) {
            last_disp = HAL_GetTick();
            char buf[20];
            snprintf(buf, sizeof(buf), "Out:%.2fVpp", output_level);
            OLED_ShowString(0, 6, buf);
        }
        
        HAL_Delay(20);
    }
}
