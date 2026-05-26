# 2023 A 单相逆变器并联系统 — 代码工程说明

## 工程结构（已升级为分层）

```
01_代码/
├── README.md
├── config.h                    # 全局可调参数（PWM/PI/ADC/保护阈值）
│
├── Core/
│   └── main.c                  # 8 状态机 + 1ms/10ms/50ms/200ms 时序
│
├── Drivers/                    # 板级抽象（STM32G4 HAL）
│   ├── pwm_full_bridge.{c,h}   # TIM1 互补 PWM 启停（新建，封装裸 TIM 操作）
│   ├── adc_sample.{c,h}        # ADC + DMA + RMS 计算
│   ├── display.{c,h}           # SSD1306 OLED
│   └── protection.{c,h}        # 软件保护（OVP / OCP / OTP）
│
├── Algorithm/                  # 与硬件解耦
│   ├── spwm.{c,h}              # SPWM 正弦表 + 角度推进（已去 HAL 依赖）
│   └── control.{c,h}           # 电压外环 PI（已去 HAL 依赖）
│
└── tests/                      # PC 端 Python 金标准（不参与 MCU 编译）
    ├── algo_reference.py       # 镜像 spwm.c + control.c 的纯 Python 实现
    └── cal_helper.py           # UART 调参 / 校准工具（含 --simulate 离线仿真）
```

## PC 端 Python 测试

```bash
# 1. 算法金标准（无依赖，即可运行）
python tests/algo_reference.py
  # 验证：SPWM 表 ±1 / RMS 0.7071，PI 阶跃收敛，软启动 530ms 收敛到 0.0001%

# 2. 调试当前 PI 参数（不需要硬件）
python tests/cal_helper.py --simulate --kp 0.020 --ki 0.005

# 3. 接 STM32 板子（需要 pyserial）
python tests/cal_helper.py --port COM3 --stat
python tests/cal_helper.py --port COM3 --tune-pi --kp 0.025 --ki 0.008
python tests/cal_helper.py --port COM3 --calib   # 交互式 V/I 校准
```

**用途**：MCU 移植后，对相同输入数据，C 输出应与 Python 金标准的绝对差 < 1%。
若不一致，先怀疑 MCU 端浮点精度 / 数据类型 / 累积溢出。

## 当前状态

代码骨架完整，所有算法层 + 模块间接口已就位。  
**可编译性依赖完成 CubeMX 配置后填补 `TODO_HAL` 标记。**  
所有算法层（spwm / control）**已可独立单元测试**。

## 与 5 标杆对齐

按 steering 规范升级，与 2024H、2025A、2025G、2024B、2021A 一致：
- ✅ Core / Drivers / Algorithm 三层
- ✅ config.h 集中所有可调参数
- ✅ 算法层不依赖 HAL
- ✅ 状态机封装在 Core/main.c
- ✅ 复用 5 标杆中的"软启动 + 预充电 + 故障"成熟模式

## 与 2025 A 共享的资产

| 模块 | 2025 A | 2023 A |
|---|---|---|
| 电压闭环 PI | `Algorithm/voltage_loop.c` | `Algorithm/control.c`（同思路）|
| RMS 计算 | `Algorithm/rms_meter.c` | `Drivers/adc_sample.c::CalcVrms` |
| 状态机 | 7 状态 | 8 状态（多了双机 / 并网模式）|
| 软启动 | RAMP_DURATION_MS = 500 | 同 |

## 引脚总表

| 引脚 | 功能 | 模块 |
|---|---|---|
| PA8 / PA7 | TIM1_CH1 / CH1N | A 桥臂 PWM 互补 |
| PA9 / PB0 | TIM1_CH2 / CH2N | B 桥臂 PWM 互补 |
| PA0 | ADC1_IN1 | 输出电压采样 |
| PA1 | ADC1_IN2 | 输出电流采样 |
| PB6 / PB7 | I2C1 SCL / SDA | OLED |
| PC13 | GPIO_IN | 启动按键 |
| PA5 | GPIO_OUT | 运行 LED |

## CubeMX 配置清单

| 外设 | 配置 |
|---|---|
| TIM1 | 中心对齐 PWM Mode 1, ARR = 4249, DTG = 68 (800ns), 启用更新中断, TRGO = Update |
| ADC1 | 12bit 扫描 IN1 → IN2, DMA 循环, 触发 = TIM1 TRGO |
| DMA1 | 半完成 + 全完成中断 |
| I2C1 | 400 kHz |
| GPIO | PC13 输入上拉, PA5 推挽输出 |
| SysTick | 1 ms |

## 状态机

```
ST_IDLE
  ↓ 按 START
ST_PRECHARGE          母线限流充电（200ms）
  ↓
ST_RAMPUP             调制比软启动 0.10 → 0.70（500ms）
  ↓
┌─────────────────────────────┐
│ ST_RUN_SINGLE   （基本要求） │ ← 默认
│ ST_RUN_PARALLEL （发挥 1）   │
│ ST_RUN_GRID_TIE （发挥 2/3） │
└─────────────────────────────┘
  ↓ 故障
ST_FAULT / ST_STOP
```

## 调参指南（按经验值）

| 参数 | 初值 | 调参 |
|---|---|---|
| `VLOOP_KP` | 0.020 | 增大 → 响应快，过大 → 振荡 |
| `VLOOP_KI` | 0.005 | 太大 → 开机超调；太小 → 稳态收敛慢 |
| `MOD_INDEX_INIT` | 0.70 | 36V 母线时合理；改母线后重算 |
| `DEAD_TIME_DTG` | 68 (800ns) | 减小 → THD 改善但有直通风险，从 1.5μs 起步逐步降 |
| `RAMP_DURATION_MS` | 500 | 太短 → 冲击大；太长 → 测试影响 |

## 反模式

❌ 在 .c 里硬编码 PID 参数（必须用 config.h）  
❌ 算法层 #include "stm32g4xx_hal.h"（已去除）  
❌ main 直接写 TIM1->CCRx 寄存器（已封装到 PWMFB）  
❌ 没有死区直接上电（一定烧 4 管）  
❌ 没有预充电直接上 48V（电流冲击）

## 兜底

- TIM1 + ADC 同步配置卡 → 用单 ADC 软件触发，采样率降到 5kHz
- DMA 中断不触发 → 改用主循环轮询 ADC EOC 标志
- 双机并联振荡 → 退到单机模式（仍能拿基本要求 50 分）
- HRTIM 不需要（TIM1 + 800ns 死区已够）

## 标杆参考

- 仪表 / DSP：`备赛系统/B_历年真题实战/2025/G_电路模型探究装置/`
- **电源天花板**：`备赛系统/B_历年真题实战/2025/A_能量回馈变流器/`（同款架构）
- 控制类：`备赛系统/B_历年真题实战/2024/H_自动行驶小车/`
- THD 测量：`备赛系统/B_历年真题实战/2021/A_信号失真度测量装置/`
- 功率分析：`备赛系统/B_历年真题实战/2024/B_单相功率分析仪/`
