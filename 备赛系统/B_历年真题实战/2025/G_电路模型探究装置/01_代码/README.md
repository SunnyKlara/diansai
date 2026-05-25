# 2025G 电路模型探究装置 —— 代码工程说明

## 工程概述

本工程实现了一个能对未知RLC电路进行自主学习建模并推理生成输出信号的装置。

**核心能力**：
- 学习阶段：扫频测量未知电路的频率响应（增益+相位），2分钟内完成
- 推理阶段：对任意周期输入信号（正弦/矩形/三角等），实时生成与未知电路相同的输出

## 文件结构

```
01_代码/
├── config.h              全局配置（引脚定义、系统参数）
├── dds_ad9833.c/.h       DDS信号发生器驱动（学习阶段产生扫频信号）
├── adc_dual_sync.c/.h    ADC双通道同步采样（精确测量增益和相位）
├── dac_output.c/.h       DAC连续输出（双缓冲，无间断波形输出）
├── freq_response.c/.h    频率响应测量与存储（扫频+DFT+查找表）
├── signal_processor.c/.h 信号处理核心（FFT→查表→IFFT）
├── main.c                系统集成（状态机+主循环）
└── README.md             本文件
```

## 模块依赖关系

```
main.c
  ├── dds_ad9833        (学习阶段)
  ├── adc_dual_sync     (学习+推理)
  ├── dac_output        (推理阶段)
  ├── freq_response     (学习阶段)
  │     ├── dds_ad9833
  │     └── adc_dual_sync
  └── signal_processor  (推理阶段)
        └── freq_response (查表)
```

## 开发环境

- **IDE**: STM32CubeIDE 1.13+
- **MCU**: STM32F407VET6 (168MHz, 192KB SRAM, 512KB Flash)
- **HAL库**: STM32Cube_FW_F4 V1.27+
- **DSP库**: CMSIS-DSP（arm_cfft_f32等）

## CubeMX配置清单

| 外设 | 配置 | 用途 |
|------|------|------|
| SPI1 | Mode 2, 8bit, MSB, 10.5MHz | AD9833 DDS |
| ADC1 | 12bit, Dual Regular Simultaneous, DMA, TIM2触发 | 输入信号采样 |
| ADC2 | 12bit, Slave of ADC1 | 输出信号采样 |
| DAC1 | OUT1, TIM6触发, DMA Circular | 推理输出 |
| TIM2 | Internal Clock, TRGO=Update | ADC采样触发 |
| TIM6 | Internal Clock, TRGO=Update | DAC输出触发 |
| I2C1 | 400kHz Fast Mode | OLED显示 |
| GPIO | PC0/1/2=按键(上拉), PC4/5=LED | 人机交互 |

## 引脚总表

| 引脚 | 功能 | 模块 |
|------|------|------|
| PA0 | ADC1_IN0 | 输入信号采样 |
| PA1 | ADC2_IN1 | 输出信号采样 |
| PA4 | SPI1_NSS(学习) / DAC1_OUT1(推理) | DDS片选 / DAC输出 |
| PA5 | SPI1_SCK | DDS时钟 |
| PA7 | SPI1_MOSI | DDS数据 |
| PB6 | I2C1_SCL | OLED |
| PB7 | I2C1_SDA | OLED |
| PC0 | GPIO_IN | 学习键 |
| PC1 | GPIO_IN | 启动键 |
| PC2 | GPIO_IN | 停止键 |
| PC4 | GPIO_OUT | 学习指示LED |
| PC5 | GPIO_OUT | 运行指示LED |

## 编译步骤

1. 用CubeMX按上述配置生成工程框架
2. 将本目录下所有.c/.h文件复制到工程的Core/Src和Core/Inc
3. 在CubeMX生成的main.c中，删除自动生成的主循环，替换为本工程的main.c内容
4. 在工程设置中启用CMSIS-DSP库（勾选"Use DSP Library"）
5. 编译，下载到STM32F407

## 调试顺序

1. **DDS验证**：设置1kHz输出，示波器看波形
2. **ADC验证**：DDS输出接ADC输入，看采样数据是否正确
3. **DAC验证**：写入正弦表到DAC缓冲区，看输出波形
4. **单频测量**：DDS输出1kHz，接一个已知RC滤波器，测量增益和相位
5. **扫频学习**：对已知RC滤波器做完整扫频，检查频率响应曲线
6. **推理验证**：输入1kHz正弦波，检查输出幅度和相位是否与实际电路一致
7. **矩形波验证**：输入1kHz矩形波，检查输出波形是否与实际电路一致
8. **未知电路测试**：换一个未知RLC电路，重新学习+推理

## 已知限制

1. 频率响应只在测量过的频率范围内准确（100Hz~100kHz）
2. 超出范围的谐波分量会被截断（增益设为边界值）
3. DAC输出有10ms的延迟（一帧的处理时间）
4. PA4引脚复用需要在学习和推理之间切换GPIO模式
