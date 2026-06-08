# 2023 A 单相逆变器并联 —— CubeMX 配置说明

> 平台：STM32G474RE Nucleo

## 1. 新建工程
- MCU 选 STM32G474RETx
- Project Manager → Toolchain = MDK-ARM V5

## 2. RCC
```
HSE: Crystal/Ceramic Resonator   24 MHz (Nucleo 板载)
SYSCLK: 170 MHz
HCLK: 170 MHz
APB1 Prescaler: /1 → 170 MHz
APB2 Prescaler: /1 → 170 MHz
```

## 3. HRTIM（关键）
```
HRTIM Master   : Master timer enable
HRTIM TIM_A    : Compare 1, 2, 3 → PWM 双极性
                 Period = 8500   (170MHz × 32 / 20kHz = 272000，除 32)
                 [实际 HRTIM 内部周期 = 5440000 ÷ 20000 = 272 ticks 显示]
                 Dead-Time = 800 ns 对应 DTG = 0x88
                 Output 1 + 1N (互补)
                 Output 2 + 2N (互补)
NVIC           : HRTIM TRGO Update interrupt enabled
```

## 4. ADC1 / ADC2
```
ADC1 IN1       : V_out
ADC1 IN6       : V_bus
ADC2 IN2       : I_out
External Trig  : HRTIM_TIM_A Compare 4 event
DMA            : DMA1 Stream 0 (ADC1) Half-word Circular
NVIC           : ADC1 EOS interrupt enabled
```

ADC2 配 Multi-Mode = Regular Simultaneous，自动跟 ADC1。

## 5. GPIO
```
PA8/PB13       : HRTIM_CHA1/CHA1N    AF13
PA9/PB14       : HRTIM_CHA2/CHA2N    AF13
PA12           : HRTIM_BKIN          AF13   (硬件保护)
PB10           : Output Push-Pull    (继电器)
PA5            : Output              (LED)
PC13           : Input Pull-Up + EXTI13 (按键)
```

## 6. I2C1 (OLED)
```
Mode: Standard 100 kHz
PB8 (SCL) / PB9 (SDA), AF4
```

## 7. LPUART1（板载 ST-Link VCP）
```
Mode: Async
Baud: 115200, 8N1
PA2 (TX) / PA3 (RX), AF12
```

## 8. NVIC 优先级
| 中断 | 优先级 |
|---|---|
| HRTIM_TRGO | 0 |
| TIM1_BRK | 0 |
| ADC1 | 1 |
| TIM6 (1ms) | 2 |
| EXTI13 (KEY) | 3 |
| LPUART1 | 4 |

## 9. Generate Code
按 Alt+K 生成 → Keil 自动打开。

## 10. 集成本仓库
```
1. 把 01_代码/Core, Drivers, Algorithm 拖入工程
2. Include 目录加：01_代码 / 01_代码/Drivers / 01_代码/Algorithm
3. 编译 (F7) → 0 errors
```

## 11. 烧录
ST-Link via USB → Flash → Download (F8)

## 12. 验证
- LED 闪烁 → 系统跑通
- 按 K_START → ST_PRECHARGE，OLED 显示 "Precharging…"
- 母线 > 32V → ST_RUN，OLED 显示 V_out / I_out
- 故意短路输出 50ms → ST_FAULT，PWM 立即关
