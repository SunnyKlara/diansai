# 2025 G 电路模型探究装置 —— CubeMX 配置说明

> STM32CubeMX 6.10+ + Keil MDK 5

## 1. 新建工程
- MCU 选 STM32F407VET6
- 项目名：2025G_circuit_explorer
- IDE：MDK-ARM V5

## 2. RCC（时钟）
```
HSE          : Crystal/Ceramic Resonator   8 MHz
SYSCLK       : 168 MHz
HCLK         : 168 MHz
APB1 Prescaler : /4 → 42 MHz
APB2 Prescaler : /2 → 84 MHz
```

> Clock Configuration 页面右侧 HCLK 应自动显示 168。

## 3. ADC1（参考端电压 V_R）
```
IN0           : Single-ended
Continuous Conversion : Disabled
External Trigger      : Timer 2 Trigger Out event
DMA Settings          : DMA2 Stream 0, Half-word, Circular, Memory address increment
NVIC                  : DMA2 Stream0 global interrupt enabled
```

## 4. ADC2（V_DUT 同步双 ADC 模式）
```
Mode                  : Regular Simultaneous mode
IN1                   : Single-ended
Trigger               : 来自 ADC1（自动）
DMA                   : 不需要（数据通过 ADC1 多模式 DR 寄存器读出）
```

实际多模式数据读法：每次 DMA 搬入的是 32 位 = ADC2[31:16] | ADC1[15:0]。

## 5. DAC1（X-Y 输出）
```
OUT1                  : External pin only / TIM6 TRGO trigger
OUT2                  : 同上
DMA                   : DMA1 Stream 5（OUT1）+ DMA1 Stream 6（OUT2）, Word, Circular
NVIC                  : 不用
```

## 6. SPI1（AD9833）
```
Mode                  : Full-Duplex Master
Baud Rate Prescaler   : 4   (84MHz/4 = 21MHz, AD9833 上限 25MHz, OK)
CPOL                  : High
CPHA                  : 1 Edge
Frame Format          : Motorola
Data Size             : 16 bits
NSS                   : Disabled (用 GPIO 软件控)
```

## 7. TIM2（ADC 触发，200 kHz）
```
Source                : Internal Clock
Prescaler             : 0
Counter Period        : (84M / 200k) - 1 = 419
Trigger Output        : Update event (TRGO)
Auto-reload preload   : Disabled
```

## 8. TIM6（DAC 触发，200 kHz）
```
Prescaler             : 0
Counter Period        : (84M / 200k) - 1 = 419
Trigger Output        : Update event
```

## 9. I2C1（OLED）
```
I2C Speed Mode        : Fast Mode
I2C Speed Frequency   : 400 kHz
GPIO Pull-up          : 默认外部 4.7kΩ + Pull-up
```

## 10. USART1（调试）
```
Mode                  : Asynchronous
Baud Rate             : 921600
Data Bits             : 8
Parity                : None
Stop Bits             : 1
NVIC                  : USART1 global interrupt enabled
```

## 11. GPIO
```
PE0   : Input / Pull-up                     (KEY_START)
PE1   : Input / Pull-up                     (KEY_MODE)
PB6   : Output / Push-pull                  (AD9833 FSYNC)
PD12  : Output / Push-pull                  (LED_R)
PD13  : Output / Push-pull                  (LED_G)
```

## 12. NVIC 优先级
| 中断 | Group | Sub |
|---|---|---|
| DMA2_Stream0 (ADC) | 0 | 0 |
| TIM2 | 1 | 0 |
| TIM6 | 2 | 0 |
| USART1 | 3 | 0 |
| EXTI0/1 | 4 | 0 |

## 13. 生成代码
```
Project → Generate Code (Alt+K)
```

会生成：
- `Core/Src/main.c, stm32f4xx_hal_msp.c, stm32f4xx_it.c`
- `Drivers/STM32F4xx_HAL_Driver/`

## 14. 集成本仓库代码
```
1. 在 Keil 工程里 Add Group "App"
2. 添加：
   01_代码/Core/main.c            （替换默认 main.c 中的 while 部分）
   01_代码/Drivers/*.c, *.h
   01_代码/Algorithm/*.c, *.h
3. 包含目录加：01_代码 / 01_代码/Drivers / 01_代码/Algorithm
4. 编译 (F7) → 0 errors
```

## 15. 烧录
```
Flash → Download (F8) 或 Debug (Ctrl+F5)
```

## 16. 检查
- 上电 LED_R 闪
- AD9833 输出 1 kHz 正弦（示波器看 SDATA / FSYNC 时序正确）
- ADC 串口打数据 0~4095 范围内
