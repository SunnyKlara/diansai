# 2025 A 能量回馈变流器 —— CubeMX 配置说明

> 与 2023A 类似（同 G474RE），但是**三相 SVPWM**，HRTIM 配置更复杂。

## 1. 新建工程
- MCU 选 STM32G474RETx
- Toolchain：MDK-ARM V5

## 2. RCC
```
HSE: 24 MHz Crystal (Nucleo 板)
SYSCLK: 170 MHz
APB1, APB2: 都 170 MHz
```

## 3. HRTIM（三相 SVPWM）

```
HRTIM Master:
  - 中心对齐 PWM (Up-Down counter)
  - Period (UPP) = 5440000 / 20000 = 272 ticks
  - Repetition = 0
  - DAC trigger event = 不用
  
HRTIM TIM_A (相 a):
  - Output 1 + 1N (互补)
  - Dead-Time DT = 500 ns (DTG = 0x55)
  - Compare 1 = duty * 272 (动态)
  - Reset Source: Master timer period
  
HRTIM TIM_B (相 b): 同 A
HRTIM TIM_C (相 c): 同 A

HRTIM TIM_E (ADC 触发):
  - Output: TRGO event = TIM_A Compare 4 (PWM 中点)
  - 用于触发 ADC1+ADC2+ADC3
  
HRTIM Fault Source:
  - FLT1 = PA12 (BKIN), 高有效, 自动停所有输出
```

## 4. ADC1 配置（含 V_bus + I_a + V_a）
```
Mode               : Multi-mode = ADC1+ADC2 Regular Simultaneous
Resolution         : 12 bit
Conversion mode    : Discontinuous, n=4 (4 通道一组)
External trigger   : HRTIM TRGO
Channels           : IN1 (Ia) → IN4 (Va) → IN6 (Vbus) → IN17 (Vb)
DMA                : DMA1 Stream 0, Half-word, Circular
Sampling time      : 8 cycles (~ 50ns @ 170MHz)
NVIC               : EOS interrupt
```

## 5. ADC2（与 ADC1 同步）
```
Multi-mode = ADC1+ADC2 Regular Simultaneous
Channels: IN2 (Ib) → IN3 (Ic) → IN13 (Vc)
DMA: DMA1 Stream 1
```

## 6. ADC3（独立或留空）
本设计不用 ADC3，已用 ADC1+ADC2 完成 7 通道。

## 7. GPIO
按引脚分配表配置。重点：
- HRTIM 6 通道 PWM 引脚配 AF13
- BKIN PA12 配 AF13 + 内部上拉
- 继电器 / LED / 按键 GPIO 模式

## 8. I2C1（OLED）
```
Speed: 100 kHz Standard
PB8/PB9, AF4
```

## 9. LPUART1（板载 ST-Link VCP）
```
Mode: Async
Baud: 921600
PA2/PA3, AF12
```

## 10. NVIC 优先级
| 中断 | Group | Sub |
|---|---|---|
| HRTIM_TRGO | 0 | 0 |
| TIM1_BRK_TIM15 (BKIN) | 0 | 1 |
| DMA1_Stream0 (ADC) | 1 | 0 |
| TIM6 (1ms) | 2 | 0 |
| EXTI13 | 3 | 0 |
| LPUART1 | 4 | 0 |

## 11. Generate

注意：CubeMX 对 HRTIM 支持有时不完整，可能需要手动调整 .ioc 后再生成，或直接写寄存器：

```c
hrtim1.Init.HRTIMInterruptResquests = HRTIM_IT_NONE;
hrtim1.Init.SyncOptions = HRTIM_SYNCOPTION_NONE;
hrtim1.Init.SyncInputSource = HRTIM_SYNCINPUTSOURCE_NONE;
hrtim1.Init.SyncOutputSource = HRTIM_SYNCOUTPUTSOURCE_MASTER_START;
hrtim1.Init.SyncOutputPolarity = HRTIM_SYNCOUTPUTPOLARITY_POSITIVE;
HAL_HRTIM_Init(&hrtim1);
```

## 12. 编译 + 烧录

```
F7 编译 → 0 errors
F8 烧录到 G474RE Nucleo
```

## 13. 验证流程

1. 上电 LED 闪烁
2. 按 K_START → ST_PRECHARGE
3. 母线 > 0.9·V_dc → ST_RUN，OLED 显示 SVPWM 波形参数
4. 三相输出测电压（万用表 + 示波器）
5. **故意短路一相 50ms** → BKIN 触发 → ST_FAULT，PWM 立即停
