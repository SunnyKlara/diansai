/**
 * 2025G 电路模型探究装置 —— 全局配置
 * 
 * 所有引脚定义、系统参数、编译开关集中在这里。
 * 修改硬件接线时只改这个文件，不动其他代码。
 */

#ifndef __CONFIG_H
#define __CONFIG_H

/* ================================================================
 * MCU: STM32F407VET6
 * 时钟: HSE 8MHz → PLL → SYSCLK 168MHz
 *       APB1 = 42MHz (TIM2~7, DAC, UART)
 *       APB2 = 84MHz (TIM1/8, ADC, SPI1)
 * ================================================================ */

/* -------- DDS AD9833 (SPI1) -------- */
#define DDS_SPI             SPI1
#define DDS_FSYNC_PORT      GPIOA
#define DDS_FSYNC_PIN       GPIO_PIN_4      // SPI1_NSS, 但用GPIO手动控制
#define DDS_SCK_PORT        GPIOA
#define DDS_SCK_PIN         GPIO_PIN_5      // SPI1_SCK
#define DDS_MOSI_PORT       GPIOA
#define DDS_MOSI_PIN        GPIO_PIN_7      // SPI1_MOSI (AD9833只需要MOSI，无MISO)
#define DDS_MCLK_FREQ       25000000UL      // AD9833参考时钟 25MHz

/* -------- ADC双通道同步采样 -------- */
// ADC1: 采样探究装置输出给被测电路的信号（即DDS经放大后的信号）
// ADC2: 采样被测电路的输出信号
// 两个ADC由TIM2 TRGO同步触发
#define ADC_INPUT_PORT      GPIOA
#define ADC_INPUT_PIN       GPIO_PIN_0      // ADC1_IN0: 输入信号采样点
#define ADC_OUTPUT_PORT     GPIOA
#define ADC_OUTPUT_PIN      GPIO_PIN_1      // ADC2_IN1: 输出信号采样点

#define ADC_SAMPLE_TIMER    TIM2            // 触发ADC采样的定时器
#define ADC_VREF            3.3f            // ADC参考电压
#define ADC_RESOLUTION      4096            // 12bit

/* -------- DAC输出 -------- */
// DAC1_OUT1 (PA4) 用于推理阶段输出合成信号
// 注意：PA4同时是DDS的FSYNC！
// 解决：学习阶段用SPI1(PA4=FSYNC)，推理阶段切换PA4为DAC输出
// 两个阶段不会同时工作，所以引脚可以复用
#define DAC_OUT_PORT        GPIOA
#define DAC_OUT_PIN         GPIO_PIN_4      // DAC1_OUT1
#define DAC_OUTPUT_TIMER    TIM6            // 触发DAC输出的定时器

/*
 * 【引脚复用的设计思考】
 * PA4在学习阶段是DDS的FSYNC（SPI片选），在推理阶段是DAC输出。
 * 这不是偷懒，而是有意为之：
 * 1. 学习和推理不会同时进行
 * 2. 节省一个引脚（STM32F407的DAC只能在PA4或PA5）
 * 3. 切换时需要重新配置GPIO模式（SPI→Analog）
 * 
 * 如果觉得复用太冒险，可以用PA5(DAC1_OUT2)作为DAC输出，
 * 但PA5已经被SPI1_SCK占用了。
 * 
 * 最稳妥的方案：用SPI2给DDS，PA4专门给DAC。
 * 代价是多占用几个引脚。赛场上根据实际情况决定。
 */

/* -------- OLED显示 (I2C1) -------- */
#define OLED_I2C            I2C1
#define OLED_SCL_PORT       GPIOB
#define OLED_SCL_PIN        GPIO_PIN_6
#define OLED_SDA_PORT       GPIOB
#define OLED_SDA_PIN        GPIO_PIN_7

/* -------- 按键 -------- */
#define BTN_LEARN_PORT      GPIOC
#define BTN_LEARN_PIN       GPIO_PIN_0      // 学习键
#define BTN_START_PORT      GPIOC
#define BTN_START_PIN       GPIO_PIN_1      // 启动键
#define BTN_STOP_PORT       GPIOC
#define BTN_STOP_PIN        GPIO_PIN_2      // 停止键

/* -------- LED指示 -------- */
#define LED_LEARN_PORT      GPIOC
#define LED_LEARN_PIN       GPIO_PIN_4      // 学习中指示
#define LED_RUN_PORT        GPIOC
#define LED_RUN_PIN         GPIO_PIN_5      // 运行中指示

/* ================================================================
 * 系统参数
 * ================================================================ */

/* 频率响应测量 */
#define FREQ_RESP_MAX_POINTS    200         // 频率响应查找表最大点数
#define SWEEP_SETTLE_CYCLES     10          // 每个频率点等待的周期数
#define DFT_SAMPLES_PER_POINT   512         // 每个频率点的采样点数

/* 信号处理 */
#define FFT_SIZE                2048        // FFT点数（必须是2的幂）
#define INFERENCE_SAMPLE_RATE   200000.0f   // 推理阶段采样率 200kHz
                                            // 覆盖到100kHz（奈奎斯特）
                                            // 50kHz信号的4倍过采样

/* DAC输出 */
#define DAC_SAMPLE_RATE         200000.0f   // DAC输出速率 = ADC采样率
                                            // 保证输入输出同频

/* 精度要求 */
#define GAIN_TOLERANCE          0.10f       // 增益误差容限 10%
#define PHASE_TOLERANCE         0.17f       // 相位误差容限 ~10°

#endif /* __CONFIG_H */
