# 2025 A 能量回馈变流器 — 代码工程说明

## 工程结构

```
01_代码/
├── README.md                # 本文件
├── config.h                 # 全局可调参数（PWM/PID/ADC/容差/任务模式）
│
├── Core/
│   └── main.c               # 7 状态机 + 时序调度
│
├── Drivers/                 # 板级抽象（HAL）
│   ├── pwm_3phase.{c,h}     # TIM1 三相互补 PWM 启停 + 死区
│   ├── adc_3phase.{c,h}     # 双 ADC 同步 + DMA + 转换 + 回调
│   ├── oled.{c,h}           # SSD1306 显示
│   ├── button.{c,h}         # 5 键扫描
│   └── led_buzzer.{c,h}     # 状态指示
│
└── Algorithm/               # 与硬件解耦的算法层
    ├── svpwm_3phase.{c,h}   # SVPWM 七段式（已重构为完全解耦）
    ├── voltage_loop.{c,h}   # 电压外环 PI（含前馈）
    ├── rms_meter.{c,h}      # 滑动窗口 RMS（覆盖 20~100Hz）
    └── feedback_control.{c,h}  # 同步整流时序（30 分核心算法）
```

## 当前状态

代码骨架完整，所有算法层 + 模块间接口已就位。  
**可编译性依赖完成 CubeMX 配置后填补 `TODO_HAL` 标记。**  
所有算法层（svpwm / voltage_loop / rms_meter / feedback_control）**已可独立单元测试**。

## CubeMX 配置清单

| 外设 | 配置 | 用途 |
|---|---|---|
| TIM1 | 中心对齐 PWM, ARR=4249, DTG=68 (800ns), 三互补输出, 启用更新中断, TRGO=Update | SVPWM 输出 |
| ADC1 | 12bit, 4 通道扫描, DMA 循环, 触发=TIM1 TRGO | Va/Vb/Vc/Vbus |
| ADC2 | 12bit, 3 通道扫描, 与 ADC1 同步 | Ia/Ib/Ic |
| DMA1 ChX | 半完成 + 全完成中断 | ADC 双缓冲 |
| I2C1 | 400kHz | OLED |
| GPIO PC0~PC4 | 输入 + 上拉 | 5 个按键 |
| GPIO（红/绿/黄）| 推挽输出 | 三 LED |
| GPIO（蜂鸣器）| 推挽输出 | 状态提示 |
| SysTick | 1ms | 系统计时 |

## 引脚清单

| 引脚 | 功能 | 模块 |
|---|---|---|
| PA8/PA7 | TIM1_CH1/CH1N | A 相桥臂 |
| PA9/PB0 | TIM1_CH2/CH2N | B 相桥臂 |
| PA10/PB1 | TIM1_CH3/CH3N | C 相桥臂 |
| PA0~PA2 | ADC1_IN1~3 | 三相电压采样（11:1 分压 + 1.65V 偏置）|
| PA3 | ADC1_IN4 | 母线电压 |
| PA4~PA6 | ADC2_IN1~3 | 三相电流（ACS712-5A）|
| PB6/PB7 | I2C1_SCL/SDA | OLED |
| PC0~PC4 | GPIO_IN | 频率+/-/启动/停止/任务切换 |
| PC4~PC6 | GPIO_OUT | 三 LED |
| PC7 | GPIO_OUT | 蜂鸣器 |

## 状态机

```
   ST_IDLE
     │ 按 START
     ▼
   ST_PRECHARGE        预充电（等 Vbus > 0.9 × Vdc_target）
     │ Vbus 达标
     ▼
   ST_RAMPUP           调制比软启动（500ms 升到稳态）
     │ Mod 达标
     ▼
   ST_RUN_INVERTER ←→ ST_RUN_FEEDBACK    （由 KEY_TASK 在 IDLE 时切换）
     │ STOP / 故障
     ▼
   ST_FAULT / ST_STOP
```

## 三环控制

```
电压外环 (10ms, PI)
   │ → m
   ▼
SVPWM (50μs, 中断)
   │ → Ta/Tb/Tc
   ▼
PWM3Phase (硬件)
   │
   ▼
物理三相输出
   │
   ▼
ADC 反馈 (50μs, DMA)
   │
   ▼
RMS 计算 (滑动窗口)
   ↑
（喂回到电压外环）

发挥模式额外开启：
ADC 反馈 → FB_Update（电流过零检测） → 写 SR MOS GPIO
```

## 算法层关键参数（详见 config.h）

| 参数 | 值 | 调参指南 |
|---|---|---|
| `VLOOP_KP` | 0.015 | 增大→响应快，过大→振荡 |
| `VLOOP_KI` | 0.003 | 太大→开机超调；太小→稳态收敛慢 |
| `RMS_BUFFER_LEN` | 512 | 必须 ≥ 最低基波周期对应的采样数 |
| `SR_THRESHOLD_A` | 0.15 | 过零滞回，太小→误开关，太大→效率损失 |
| `RAMP_DURATION_MS` | 500 | 软启动时间，太短→冲击，太长→影响测试 |
| `DEAD_TIME_DTG` | 68 | 800ns，权衡 THD 与穿通保护 |

## 调试顺序

按 `04_调试记录/调试检查清单.md` 严格执行，关键 milestone：

- [ ] 闪 LED（编译 + 烧录通畅）
- [ ] 串口 printf
- [ ] PWM 单通道输出（占空比 50%，测电压 = Vdc/2）
- [ ] PWM 三通道互补，死区 800ns（示波器验证）
- [ ] ADC 单通道采集，已知信号验证
- [ ] ADC 双 ADC 同步（PA0/PA1 短路应读相同值）
- [ ] SVPWM 开环输出，3.3V 测试电压看到三相正弦
- [ ] 接小功率负载（比如 10Ω 三相），看 LC 滤波后波形
- [ ] 加 RMS 闭环
- [ ] 加电压闭环 PI（验证负载调整率 < 0.3%）
- [ ] 验证 THD（用功率分析仪或 FFT 软件）
- [ ] 频率扫 20 / 50 / 100 Hz
- [ ] 切到 RUN_FEEDBACK 模式（先用二极管整流验证 I1 ≥ 1A）
- [ ] 加同步整流（**最关键**，先用低电压验证时序，再升压）
- [ ] 测 Pd（功率分析仪测 P_in - P_out）

## 反模式（千万别这么干）

❌ 在 .c 文件里硬编码 PID 参数（必须放 config.h）  
❌ 算法层直接调 HAL 函数（必须通过 driver 层）  
❌ 没有死区直接上电（一定烧 MOS）  
❌ 没有预充电直接上 48V（电流冲击烧二极管）  
❌ 跳过 RAMP 直接到目标调制比（电压超调可能烧负载）  
❌ 同步整流没验证就上高功率（一旦时序错就烧 6 管）

## 兜底方案

- TIM1 / ADC 同步配置卡死 → 改用单 ADC + 软件触发，采样率降到 10kHz
- HRTIM 没用（题目用 TIM1 即可，HRTIM 仅供后续优化）
- 同步整流时序错 → 立刻 `USE_SYNC_RECTIFIER 0` 退回二极管模式（仍拿 15 分）
- 闭环不收敛 → 退回开环（前馈调制比 m_ff），手动设定输出电压

## 与历史代码的演进

| 旧（扁平）| 新（分层）| 改动 |
|---|---|---|
| `01_代码/main.c` | `Core/main.c` | 重写：7 状态机、含预充电、软启动、故障处理；ADC 回调链路接通 |
| `01_代码/svpwm_3phase.{c,h}` | `Algorithm/svpwm_3phase.{c,h}` | 重构：移除 HAL 依赖，通过 PWM3Phase_SetDuty 写入 |
| —（无）| `config.h` | 新建：所有可调参数集中 |
| —（断裂）| `Drivers/adc_3phase` | 新建：ADC + DMA + 转换 + 弱回调 |
| —（无）| `Algorithm/voltage_loop` | 新建：含前馈的电压闭环 PI |
| —（无）| `Algorithm/rms_meter` | 新建：滑动窗口 RMS |
| —（无）| `Algorithm/feedback_control` | 新建：**同步整流核心算法（30 分关键）** |
