# 2025 B 单相 APF —— 工程编译/烧录说明

## 目录结构

```
01_代码/
├── config.h                      # 全局可调参数中心（PI、采样率、阈值）
├── Core/
│   └── main.c                    # 5 状态机：IDLE → PRECHARGE → MEAS → APF / FAULT
├── Drivers/                      # 板级抽象层（HAL 仅在这里出现）
│   ├── adc_us_il.{c,h}           # 双 ADC 同步采样 + DMA + 软件自校零
│   ├── pwm_apf.{c,h}             # H 桥互补 PWM 启停 + 死区
│   ├── lcd_tft.{c,h}             # 2.8" SPI TFT 波形显示
│   └── button.{c,h}              # 4 按键扫描 + 软件去抖
└── Algorithm/                    # 与硬件解耦（不依赖 HAL）
    ├── harmonic_fft.{c,h}        # 1024 点 FFT + 汉宁窗 + H₁..H₁₀ + THD
    └── apf_compensator.{c,h}     # 滑动 DFT 提基波 + 电流跟踪 PI
```

## 编译

### 方案 A：Keil MDK / STM32CubeIDE 真机

1. 用 STM32CubeMX 新建工程，MCU 选 **STM32F407VET6**
2. 配置外设：
   - HSE 8 MHz, PLL → SYSCLK 168 MHz
   - **TIM1**：中心对齐 PWM, ARR = 4200（20kHz）, 死区 84 ticks
   - **ADC1+ADC2**：同步双通道，TIM1 TRGO 触发，DMA1 Stream 0
   - **TIM3**：1ms 基础定时器（SysTick 替代）
   - **SPI2**：18 MHz 主机模式（连 LCD）
   - **GPIO**：PE0..PE3 上拉输入（按键）；PA8/PA9/PB13/PB14 替补 PWM 输出
3. 把本目录文件加入工程：
   ```
   Source Group:
     Core/main.c
     Drivers/adc_us_il.c
     Drivers/pwm_apf.c
     Drivers/lcd_tft.c
     Drivers/button.c
     Algorithm/harmonic_fft.c
     Algorithm/apf_compensator.c
   ```
4. 在 `harmonic_fft.c` 顶部 `#define HARMONIC_USE_CMSIS` 启用 CMSIS-DSP（需要把 CMSIS-DSP 库加入工程）
5. 在 `Drivers/*.c` 中找 `_hal_xxx` 占位函数，替换为 CubeMX 生成的 HAL 调用
6. 烧录：ST-Link / DAP-Link → SWD

### 方案 B：PC 端单元测试（GCC）

```bash
# 算法层独立编译验证（不依赖 HAL）
gcc -DAPF_UNIT_TEST -I . \
    Algorithm/harmonic_fft.c \
    Algorithm/apf_compensator.c \
    -lm -o /tmp/test
```

只编译 Algorithm/*.c 即可，driver / core 包含 HAL 占位钩子，PC 编译只需保留空实现。

## 关键参数调试节奏（已写入 config.h）

| 参数 | 起步值 | 目标值 | 调试顺序 |
|---|---|---|---|
| 死区时间 | 1 μs | 500 ns | 上电前确认 |
| Kp | 5.0 | 实测调 | 先开环看波形跟随 |
| Ki | 500 | 实测调 | Kp 稳定后再加 |
| 软启 ramp | 1 s | 1 s | 不动 |
| 故障保护阈值 | 6 A | 实测调 | 上电前确认 |

## 中断挂接（CubeMX 生成 stub 后修改）

```c
void TIM1_UP_TIM10_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        main_ctrl_loop_isr();
    }
}

void DMA1_Stream0_IRQHandler(void)
{
    /* 真机：取 ADC1/ADC2 双通道结果，调用 adc_us_il_on_sample() */
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    main_systick_isr();
}
```

## 与标杆题的复用关系

| 模块 | 来源 | 复用度 |
|---|---|---|
| `Algorithm/harmonic_fft.{c,h}` | 2025 G + 2024 B 同款 FFT 思路 | 90% |
| `Drivers/adc_us_il.{c,h}` | 2024 B 双 ADC 同步采样 | 85% |
| `Drivers/pwm_apf.{c,h}` | 2023 A / 2025 A H 桥 PWM | 80% |
| `Drivers/button.{c,h}` | 2024 H 按键去抖 | 95% |

## 已知 TODO

- [ ] CubeMX 生成 HAL 骨架 + 替换 `_hal_xxx` 占位
- [ ] 验证 ADC 双同步采样的相位差（理论 = 0，实测应 < 0.5 个采样点）
- [ ] 调 PI 系数：先 Kp = 2 → 8 step，Ki 维持 0；稳后再加 Ki
- [ ] 软件保护实测：故意短路 / 拔掉互感器 → 必须进 ST_FAULT
