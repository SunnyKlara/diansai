/**
 * 2021A 信号失真度测量装置 —— 全局配置
 * MCU: TI MSP432P401R (48MHz, 14bit ADC, 256KB Flash, 64KB RAM)
 * 
 * ============================================================
 * 【为什么每个数字都不是随便填的】
 * ============================================================
 * 
 * 这个文件里的每个参数都经过了误差预算的验证。
 * 改任何一个数字之前，先想清楚它会影响什么。
 * 
 * 例如：FFT_SIZE从1024改成2048
 *   好处：频率分辨率翻倍（从97Hz变成49Hz）
 *   代价：RAM占用翻倍（从8KB变成16KB），处理时间翻倍
 *   结论：1024对于1kHz基频已经足够（分辨率97Hz，基频在bin 10）
 *         但对于100kHz基频可能不够（分辨率97Hz，5次谐波在bin 5120→超出范围）
 *         所以发挥部分可能需要2048
 * 
 * ============================================================
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <ti/devices/msp432p4xx/driverlib/driverlib.h>

/* ================================================================
 * 系统时钟
 * MSP432P401R: MCLK最高48MHz, SMCLK最高24MHz
 * ADC时钟源: SMCLK (24MHz)
 * ADC采样率: SMCLK / (采样时间+转换时间)
 *   14bit转换需要16个ADC时钟 = 16/24MHz = 0.67μs
 *   加上采样时间(最少4个时钟) = 20/24MHz = 0.83μs
 *   最高采样率 ≈ 1/0.83μs ≈ 1.2MSPS
 *   实际留余量，用1MSPS
 * ================================================================ */

#define SYSTEM_CLOCK_HZ     48000000UL
#define SMCLK_HZ            24000000UL

/* ================================================================
 * ADC配置
 * ================================================================ */

// ADC输入引脚
// 【选P5.5(A0)的理由】
// MSP432的ADC14有24个输入通道，但不是所有引脚都方便。
// P5.5是A0通道，在LaunchPad上有明确的标识，接线不容易搞错。
#define ADC_INPUT_PORT      GPIO_PORT_P5
#define ADC_INPUT_PIN       GPIO_PIN5
#define ADC_INPUT_CHANNEL   ADC_INPUT_A0

// ADC参数
#define ADC_BITS            14          // MSP432 ADC14是14位
#define ADC_MAX_VALUE       16383       // 2^14 - 1
#define ADC_MID_VALUE       8192        // 中间值（偏置点）
#define ADC_VREF            3.3f        // 参考电压

// 初始采样率（用于粗测频率）
#define INITIAL_SAMPLE_RATE 100000.0f   // 100kHz，对1kHz信号有100倍过采样

// 最高采样率（发挥部分用）
#define MAX_SAMPLE_RATE     1000000.0f  // 1MSPS

/* ================================================================
 * FFT配置
 * ================================================================ */

#define FFT_SIZE            1024        // FFT点数
// 【为什么是1024而不是512或2048？】
// 512：频率分辨率=fs/512，对于100kHz采样率=195Hz → 1kHz基频在bin 5，太粗
// 1024：频率分辨率=fs/1024=97Hz → 1kHz基频在bin 10，够用
// 2048：精度更好但RAM占用16KB（MSP432总共64KB），留给其他变量的空间不够
// 折中选1024

// FFT缓冲区占用的RAM：1024 × 2(复数) × 4(float) = 8192字节 = 8KB
// MSP432有64KB RAM，8KB占12.5%，可以接受

/* ================================================================
 * 信号调理
 * ================================================================ */

// 量程定义
#define RANGE_HIGH          0   // 高量程：直通，300mV~600mV
#define RANGE_LOW           1   // 低量程：×10放大，30mV~60mV

// 量程切换引脚（控制CD4051模拟开关）
#define RANGE_SEL_PORT      GPIO_PORT_P2
#define RANGE_SEL_PIN       GPIO_PIN0

// 量程自动切换阈值
// 如果ADC峰值 < AUTO_RANGE_THRESHOLD，切换到低量程
// ADC_MID_VALUE ± 200 对应约 ±40mV（高量程下）
// 300mVpp信号的ADC峰值约 ±740
// 设阈值为400，低于此值说明信号太小需要放大
#define AUTO_RANGE_THRESHOLD 400

// 各量程的电压换算系数
// 高量程：Vin = (ADC - 8192) / 8192 * 1.65V * 1.0（直通）
// 低量程：Vin = (ADC - 8192) / 8192 * 1.65V / 10.0（放大了10倍）
#define V_SCALE_HIGH        (3.3f / ADC_MAX_VALUE)
#define V_SCALE_LOW         (3.3f / ADC_MAX_VALUE / 10.0f)

/* ================================================================
 * 显示
 * ================================================================ */

// OLED (I2C, UCB0)
#define OLED_I2C_PORT       GPIO_PORT_P1
#define OLED_SCL_PIN        GPIO_PIN7   // P1.7 = UCB0SCL
#define OLED_SDA_PIN        GPIO_PIN6   // P1.6 = UCB0SDA
#define OLED_I2C_ADDR       0x3C

/* ================================================================
 * 蓝牙 (UART, UCA2)
 * ================================================================ */

#define BT_UART_PORT        GPIO_PORT_P3
#define BT_TX_PIN           GPIO_PIN3   // P3.3 = UCA2TXD
#define BT_RX_PIN           GPIO_PIN2   // P3.2 = UCA2RXD
#define BT_BAUD_RATE        115200

/* ================================================================
 * 按键与指示
 * ================================================================ */

#define BTN_START_PORT      GPIO_PORT_P1
#define BTN_START_PIN       GPIO_PIN1   // LaunchPad板载按键S1

#define LED_BUSY_PORT       GPIO_PORT_P1
#define LED_BUSY_PIN        GPIO_PIN0   // LaunchPad板载LED1(红)

#define LED_DONE_PORT       GPIO_PORT_P2
#define LED_DONE_PIN        GPIO_PIN1   // LaunchPad板载LED2(绿)

#define BUZZER_PORT         GPIO_PORT_P2
#define BUZZER_PIN          GPIO_PIN4

/* ================================================================
 * 精度目标
 * ================================================================ */

// 基本要求：THD误差 ≤ 5%（绝对值）
// 发挥部分：THD误差 ≤ 3%（绝对值）
// 我们的误差预算：总误差 < 1.1%（见深度审题文档）
// 余量：基本要求有4倍余量，发挥部分有2倍余量

#define THD_ERROR_TARGET_BASIC      5.0f    // 基本要求误差限
#define THD_ERROR_TARGET_ADVANCED   3.0f    // 发挥部分误差限

#endif /* __CONFIG_H */
