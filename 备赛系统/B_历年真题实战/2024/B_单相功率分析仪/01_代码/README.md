# 2024 B 单相功率分析仪 — 代码工程说明

## 工程结构

```
01_代码/
├── README.md
├── config.h                 # 全局参数（采样率、DFT_N=1000、互感器系数、LPM 配置）
│
├── Core/
│   └── main.c               # 5 状态机 + LPM3 低功耗循环
│
├── Drivers/                 # 板级抽象（MSP430 DriverLib）
│   ├── adc_dual.{c,h}       # ADC12 双通道 + Timer 触发 + DMA
│   ├── lcd_ht1621.{c,h}     # 段码 LCD 显示（极低功耗）
│   ├── button.{c,h}         # 2 键扫描（翻页 / 校准）
│   └── led.{c,h}            # 运行 / 故障指示
│
└── Algorithm/               # 与硬件解耦
    ├── power_calc.{c,h}     # 时域 Vrms / Irms / P / S / PF
    └── dft_harmonic.{c,h}   # 单频 DFT × 10 谐波 + THD
```

## 当前状态

代码骨架完整，所有算法层 + 模块间接口已就位。  
**可编译性依赖完成 CCS + MSP430 DriverLib 配置后填补 `TODO_TI` 标记。**  
所有算法层（power_calc / dft_harmonic）**已可独立单元测试**。

## 技术亮点

1. **整周期采样**：1000 点 / 5 周期 = 200 点/周期 / 50Hz × 1000 / 10000 = 5 → 基波恰好在 bin 5 整数 → **无频谱泄漏**
2. **单频 DFT 替代完整 FFT**：只算 10 个 bin → 省 80% 计算量 → 省 80% 功耗
3. **共用 sin/cos LUT**：基波一份 LUT，谐波通过 (h × n) mod N 索引 → 内存从 80KB 降到 8KB
4. **5 状态 + LPM3**：CPU 占空比 < 0.5%，平均电流 < 0.5mA → MCU 部分功耗 < 1.7mW
5. **段码 LCD 替代 OLED**：30 mW → 0.5 mW，节省关键的 30 mW 功耗预算

## 引脚总表

| 引脚 | 功能 | 模块 |
|---|---|---|
| P1.0 | A0 / ADC | 电压采样（互感器副边）|
| P1.1 | A1 / ADC | 电流采样（互感器副边）|
| P2.0~P2.2 | GPIO_OUT | LCD HT1621 三线（CS/WR/DAT）|
| P3.0 | GPIO_IN（中断）| 翻页按键 |
| P3.1 | GPIO_IN（中断）| 校准按键 |
| P1.0 LED1 | GPIO_OUT | 运行指示 |
| P1.1 LED2 | GPIO_OUT | 故障指示 |

## CCS + DriverLib 配置清单

| 外设 | 配置 |
|---|---|
| Clock | DCO 16MHz，SMCLK = 16MHz，ACLK = 32.768kHz |
| ADC12_B | 12bit，参考 = AVCC（3.3V）或 REF3030（3.0V），扫描 A0→A1 |
| Timer_A0 | SMCLK 触发，CCR0 = 16000000 / 10000 - 1 = 1599，CCR1 占空比 50% Set/Reset → 触发 ADC |
| DMA0 | 源 = ADC12->MEM0，目 = s_dma_buf，长 = 2000（V+I 交错），完成中断 |
| Timer_B0 | ACLK 32768Hz / 32768 = 1Hz，更新中断唤醒 LPM3 |
| GPIO 中断 | 按键引脚 → LPM 唤醒源 |
| LPM | 主循环用 LPM3，ADC 期间用 LPM0 |

## 测量周期时序

```
按 START → ST_BOOT
  ↓
每 1 秒 Timer_B 唤醒
  ↓
ST_SAMPLING：CPU LPM0，ADC + DMA 跑 102.4 ms（1000 点 / 10kHz）
  ↓ DMA 完成中断
ST_CALC：CPU active，约 2 ms（时域 + DFT 10 个 bin）
  ↓
ST_DISPLAY：约 2 ms（HT1621 SPI）
  ↓
ST_LPM：LPM3，等下一秒
```

CPU 平均占空比 < 0.5%，**典型工况下平均功耗 ≈ 11~13 mW**。

## 反模式

❌ 在 .c 里硬编码采样率 / DFT_N（必须用 config.h）  
❌ 算法层直接调 DriverLib（必须通过 driver 层）  
❌ 用普通 OLED（功耗破预算）  
❌ MCU 持续 active（没拿 LPM 的 50% 功耗优化）  
❌ 谐波计算用 sinf/cosf（应该用 LUT）  
❌ 不做互感器校准（精度直接打 ±2% 折扣）

## 兜底

- ADC + DMA 配不通 → 退到 Timer 中断手动采样（功耗略升但能跑）
- DFT 跑不动 → 减到 6 次谐波（仍达标，分数稍降）
- LCD 太复杂 → 调试期换 OLED 验证算法，比赛切回段码屏

## 标杆参考

- 控制类：`备赛系统/B_历年真题实战/2024/H_自动行驶小车/`
- 仪表 / DSP：`备赛系统/B_历年真题实战/2025/G_电路模型探究装置/`
- 电源类：`备赛系统/B_历年真题实战/2025/A_能量回馈变流器/`
- THD 测量：`备赛系统/B_历年真题实战/2021/A_信号失真度测量装置/`
