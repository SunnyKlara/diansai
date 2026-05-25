# 2021 A 信号失真度测量装置 — 代码工程说明

## 工程结构（已升级为分层）

```
01_代码/
├── README.md                # 本文件
├── config.h                 # 全局可调参数（每个参数都有计算依据）
│
├── Core/
│   └── main.c               # 一键测量状态机 + 5 次平均
│
├── Drivers/                 # 板级抽象（MSP432 DriverLib）
│   ├── adc_capture.{c,h}    # ADC14 + Timer_A 触发 + DMA 采样
│   ├── oled.{c,h}           # SSD1306 显示 + 波形 + 柱状图
│   ├── bt_hc05.{c,h}        # 蓝牙 UART 串口
│   ├── button.{c,h}         # S1 启动键
│   └── led_buzzer.{c,h}     # 红/绿 LED + 蜂鸣
│
└── Algorithm/               # 与硬件解耦
    ├── freq_detect.{c,h}    # 插值过零检测 + 同步采样率计算
    └── thd_measure.{c,h}    # FFT + 平顶窗 + 谐波提取 + THD 计算
```

## 当前状态

代码骨架完整，所有算法层 + 模块间接口已就位。  
**可编译性依赖完成 CCS + DriverLib 配置后填补 `TODO_TI` 标记。**  
所有算法层（freq_detect / thd_measure）**已可独立单元测试**（PC 端 mock CMSIS-DSP 即可跑）。

## 技术亮点

1. **同步采样消除频谱泄漏**：先粗测频率，再调整采样率使 FFT 点数恰好覆盖整数个周期
2. **插值过零检测**：线性插值将频率测量精度从 1% 提升到 0.1%
3. **平顶窗双保险**：即使同步不完美，平顶窗也能保证幅度精度 < 0.01dB
4. **自动量程切换**：30mV~600mV 全范围自动适应
5. **5 次平均降噪**：随机误差降低 √5 ≈ 2.2 倍
6. **误差预算闭环**：每个误差源都量化（详见深度审题第 2 层），总误差 < 1.1%

## 引脚总表

| 引脚 | 功能 | 模块 |
|---|---|---|
| P5.5 | ADC14_A0 | 信号输入 |
| P2.0 | GPIO_OUT | 量程切换（CD4051 控制）|
| P1.6 | UCB0SDA | OLED 数据 |
| P1.7 | UCB0SCL | OLED 时钟 |
| P3.2 | UCA2RXD | 蓝牙接收 |
| P3.3 | UCA2TXD | 蓝牙发送 |
| P1.1 | GPIO_IN（上拉）| 启动按键 S1 |
| P1.0 | GPIO_OUT | 忙指示 LED（红）|
| P2.1 | GPIO_OUT | 完成指示 LED（绿）|
| P2.4 | GPIO_OUT | 蜂鸣器 |

## 一键测量时序

```
按键
  ↓
COARSE 粗采样 100kHz × 1024 点 (10ms)
  ↓
FREQ   过零 + 插值（< 1ms）
  ↓
RANGE  自动量程切换 (1ms)
  ↓
SYNC   同步采样率计算（< 1ms）
  ↓
[CAPTURE 同步采样 (10~50ms) → FFT (10ms) → THD (< 1ms)] × 5 次平均
  ↓
DISPLAY OLED 显示 (100ms)
  ↓
BT     蓝牙发送 (200ms)
  ↓
DONE   蜂鸣 + LED 绿
```

总时间约 600ms，**远小于 10s 限制**。

## 开发环境

- **IDE**：Code Composer Studio (CCS) 12.x
- **SDK**：MSP432 SimpleLink SDK 4.x
- **DSP 库**：CMSIS-DSP（手动添加）

## 编译步骤

1. 在 CCS 中创建 MSP432P401R 工程
2. 导入 MSP432 DriverLib
3. 添加 CMSIS-DSP 库（`arm_cortexM4lf_math.lib`）
4. 把本目录下所有 `.c/.h` 添加到工程
5. 在工程 Include Search Path 加入：`./` `./Core` `./Drivers` `./Algorithm`
6. 用 SimpleLink SDK 配置工具生成外设初始化代码（替换 `TODO_TI` 占位）
7. 编译下载

## 反模式

❌ 在 .c 里硬编码采样率 / FFT 点数（必须用 config.h）  
❌ 算法层直接调 DriverLib（必须通过 driver 层）  
❌ 跳过粗测频率直接 FFT（频谱泄漏导致 THD 偏差大）  
❌ 不做量程切换（30mV 信号会被淹没在 ADC 量化噪声里）  
❌ 让 AI 一次写整个 main.c（一定有坑）

## 兜底方案

- ADC14 DMA 配置不通 → 退到中断模式（性能略降但能跑）
- MSP432 1MSPS 不够（高频信号谐波超出）→ 换 F28379D（3.5MSPS）
- 同步采样率算错 → 平顶窗仍能保证幅度精度
- 5 次平均时间不够 → 减到 3 次（仍 < 10s）

## 标杆参考

- 控制类：`备赛系统/B_历年真题实战/2024/H_自动行驶小车/`
- 仪表 / DSP：`备赛系统/B_历年真题实战/2025/G_电路模型探究装置/`
- 电源：`备赛系统/B_历年真题实战/2025/A_能量回馈变流器/`
