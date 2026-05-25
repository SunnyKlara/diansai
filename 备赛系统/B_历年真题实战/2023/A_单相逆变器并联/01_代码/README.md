# 2023A 代码工程说明

## 开发环境
- IDE: STM32CubeIDE 1.13+ 或 Keil MDK 5.38+
- MCU: STM32G474RET6 (170MHz, 128KB SRAM, 512KB Flash)
- HAL库版本: STM32Cube_FW_G4 V1.5.0+

## 文件结构
```
01_代码/
├── main.c           主程序：初始化、主循环、中断服务
├── spwm.c/.h        SPWM正弦表生成与查表
├── control.c/.h     电压闭环PI控制器
├── adc_sample.c/.h  ADC DMA双通道采样 + RMS计算
├── protection.c/.h  过压过流保护
├── display.c/.h     OLED SSD1306显示驱动
└── README.md        本文件
```

## 引脚分配

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA8  | TIM1_CH1 | 全桥A桥臂高侧PWM |
| PA7  | TIM1_CH1N | 全桥A桥臂低侧PWM（互补） |
| PA9  | TIM1_CH2 | 全桥B桥臂高侧PWM |
| PB0  | TIM1_CH2N | 全桥B桥臂低侧PWM（互补） |
| PA0  | ADC1_IN1 | 输出电压采样 |
| PA1  | ADC1_IN2 | 输出电流采样 |
| PB6  | I2C1_SCL | OLED时钟 |
| PB7  | I2C1_SDA | OLED数据 |
| PC13 | GPIO_IN | 启动按键（低有效） |
| PA5  | GPIO_OUT | 运行指示LED |

## CubeMX配置要点

### TIM1 (PWM)
- 预分频: 0 (170MHz)
- ARR: 8499 (170MHz/8500 = 20kHz)
- CH1/CH2: PWM Generation CH1/CH2 + 互补输出
- 死区时间: 136 (800ns @ 170MHz)
- 使能更新中断

### ADC1
- 分辨率: 12bit
- 扫描模式: 使能（2通道）
- DMA: 循环模式
- 触发源: TIM1 TRGO (与PWM同步采样)
- 采样时间: 47.5 cycles

### I2C1
- 速度: 400kHz (Fast Mode)
- 地址: 7bit

## 编译与下载
1. 用CubeMX生成工程框架
2. 将本目录下的 .c/.h 文件复制到工程的 Core/Src 和 Core/Inc
3. 在CubeMX生成的 main.c 中调用我们的初始化和主循环
4. 编译下载

## 调参指南
- `control.c` 中的 KP_V 和 KI_V 需要实际调参
- 先设 KI_V=0，只用P控制，逐步增大KP_V到输出开始振荡
- 然后KP_V减半，逐步增大KI_V消除稳态误差
- `adc_sample.h` 中的 VOLTAGE_SCALE 需要根据实际分压电阻校准
