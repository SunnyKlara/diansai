# Design — 2025 B 单相有源电力滤波

> 与 2023 A / 2025 A 是电源类三胞胎：单相逆变 / 三相逆变 + 同步整流 / 单相 APF。
> 共享 SVPWM/SPWM、电压闭环 PI、互感器采样等核心资产。

## 总体方案

**架构**：220V → 隔离变压器 → 24V → 非线性负载（4 种 KD/KC 组合）→ 主电流 iL  
APF 通过 H 桥从 48V 母线产生补偿电流 iF，让 iS = iL - iF 成为纯正弦。

## 关键技术决策

| 决策项 | 选择 | 理由 |
|---|---|---|
| MCU | STM32F407 | 片内 ADC + DMA + 浮点（与 2025 G 同款）|
| 谐波算法 | 滑动 DFT 提基波 + 时域差 | 比完整 FFT 快，CPU 占用小 |
| 电流跟踪 | PI 跟踪 + 限幅（不用滞环）| 开关频率固定 20kHz，便于滤波 |
| H 桥拓扑 | 单相全桥 + IR2110 | 与 2023 A 完全相同，复用驱动板 |
| 采样 | 双 ADC 同步（uS + iL） | 测谐波必须时间对齐 |

## 软件分层

```
01_代码/
├── config.h
├── Core/
│   └── main.c                         # 5 状态机 + APF 切换 + LCD 刷新
├── Drivers/
│   ├── adc_us_il.{c,h}                # 双通道 uS / iL 同步采样 + DMA
│   ├── pwm_apf.{c,h}                  # APF H 桥 PWM 启停（与 2023A 几乎一样）
│   ├── lcd_tft.{c,h}                  # 2.8" SPI TFT 波形显示
│   └── button.{c,h}                   # KD/KC/APF 切换按键
└── Algorithm/
    ├── harmonic_fft.{c,h}             # FFT × N 谐波 + THD（基本要求 3，22 分）
    └── apf_compensator.{c,h}          # 滑动 DFT 提基波 + 电流跟踪 PI（发挥）
```

## 核心算法

### 1. 滑动 DFT 提基波（apf_compensator）

设缓冲一周期 = 400 点 @ 20kHz / 50Hz：
- a₁ = (2/N) Σ iL[k] · sin(2πk/N)
- b₁ = (2/N) Σ iL[k] · cos(2πk/N)
- iL_fund(t) = a₁·sin(ωt) + b₁·cos(ωt)
- iF_ref = iL - iL_fund（谐波分量）

### 2. 电流跟踪 PI

```
err = iF_ref - iF_act
u   = Kp · err + Ki · ∫err
duty = 0.5 + u
```

PWM 频率 20kHz，电感 2mH，电流环带宽 2kHz。

## 状态机

```
ST_IDLE → ST_MEAS_ONLY    （基本要求：测 uS / iL / 谐波，不开 APF）
            ↓ 按 APF_ON
       ST_APF_RUN          （发挥：开 APF 补偿）
            ↓ 异常 / 停止
       ST_FAULT / ST_STOP
```

## 风险

| 风险 | 缓解 |
|---|---|
| iF 相位反了 → 补偿越补越差 | 先用 50% 系数 ramp，看 iS 波形改善方向 |
| 电流跟踪不及时（iF 滞后于参考）| 提高 PI 带宽 / 降低 L |
| LCD 波形显示卡 | 刷新率降到 5 Hz（人眼足够）|
