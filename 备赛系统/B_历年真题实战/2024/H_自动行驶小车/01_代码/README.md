# 2024 H 自动行驶小车 — 工程代码说明

## 工程结构

```
01_代码/
├── README.md                # 本文件
├── config.h                 # 全局可调参数（PID/PWM/引脚/距离/阈值）
│
├── Core/
│   └── main.c               # 主状态机 + 时序调度
│
├── Drivers/                 # 板级抽象（MSPM0 driverlib）
│   ├── motor.{c,h}          # TB6612 PWM + 方向
│   ├── encoder.{c,h}        # 双 TIMG 编码器接口
│   ├── ir_sensor.{c,h}      # 5 路 TCRT5000 数字读取
│   ├── imu_mpu6050.{c,h}    # I2C + DMP 读 yaw
│   ├── button.{c,h}         # 4 键扫描（START/STOP/TASK/RESET）
│   └── led_buzzer.{c,h}     # 3 LED + 蜂鸣
│
└── Algorithm/               # 与硬件解耦的算法层
    ├── pid.{c,h}            # 通用 PID（位置式/增量式）
    ├── path.{c,h}           # 任务 1/2/3/4 段序列
    ├── line_drive.{c,h}     # 直线段：IMU 航向闭环 + 编码器测距
    └── track.{c,h}          # 弧线段：红外循迹差速 PID
```

## 当前状态

代码骨架已完整，可编译性依赖完成 SysConfig 配置后填补 `TODO_MSPM0` 标记。
所有算法层（pid/path/line_drive/track）**已可独立单元测试**。

## 如何使用 CCS Theia 导入

1. **下载工具链**：
   - CCS Theia：<https://www.ti.com/tool/CCSTUDIO>
   - MSPM0 SDK：<https://www.ti.com/tool/MSPM0-SDK>（建议 v2.x+）

2. **新建工程**：
   - File → New → CCS Project
   - Target: `MSPM0G3507`
   - Connection: `Texas Instruments XDS110 USB Debug Probe`
   - Project template: `Empty Project (with main.c)`

3. **复制源码**：
   - 把本目录下 `Core/` `Drivers/` `Algorithm/` `config.h` 整体复制到工程根
   - 在工程的 Include Search Path 中加入：`./` `./Core` `./Drivers` `./Algorithm`

4. **SysConfig 配置**（关键，参考《02_硬件/电路设计说明.md》引脚映射表）：
   - **PWM**：TIMA0_C2/C3 → PA8/PA9，频率 50kHz，分辨率 1000
   - **GPIO 输出**：PB0~PB4（电机方向 + STBY），PA21~PA24（LED + 蜂鸣）
   - **GPIO 输入**：PB5~PB9（红外），PA18~PA21（按键，弱上拉）
   - **编码器**：TIMG6（PA12/PA13），TIMG7（PA16/PA17），Quadrature 模式
   - **I2C**：I2C1（实际引脚视 SysConfig 提示，注意避开冲突）
   - **UART0**：默认 BackChannel UART（XDS110），用于串口调试
   - **ADC0**：CH0 → PA25（电池电压采样）
   - **SysTick**：1ms 中断

5. **填 TODO_MSPM0**：
   - 用 SysConfig 生成的 `ti_msp_dl_config.h` 中的引脚常量替换占位
   - 把 `DL_GPIO_*`、`DL_TimerA_*`、`DL_TimerG_*`、`DL_I2C_*` 等 driverlib API 填入相应文件

6. **编译 + 烧录**：
   - 顶部 Hammer 图标编译
   - Bug 图标启动调试 → 自动烧录到 LP-MSPM0G3507

## 调试顺序（强制）

按 `04_调试记录/调试检查清单.md` 走，**严格不要跳步**。最关键的几个里程碑：

- [ ] 闪灯（验证编译 + 烧录链路）
- [ ] 串口 printf "hello"
- [ ] PWM 占空比 50% → 电机能转
- [ ] 编码器：手转一圈 → 串口打印 1320 ±5 脉冲
- [ ] I2C 读 MPU6050 WHO_AM_I → 0x68
- [ ] DMP 输出 yaw → 旋转 90° 应得 ±90°
- [ ] 红外 5 路：手放白纸/黑线下方 → 串口对应位变化
- [ ] 任务 (1)：A→B 直线 1m，IMU 闭环开关对比

## 算法可调参数速查

所有调参集中在 `config.h`：

| 参数 | 含义 | 调参建议 |
|---|---|---|
| `PID_YAW_KP` | 直线段航向 P | 太小 → 偏不回，太大 → 蛇形振荡。从 2.0 开始 |
| `PID_YAW_KD` | 直线段航向 D | 一般是 KP 的 0.3~0.7 倍 |
| `PID_TRACK_KP` | 循迹 P | 弧线段，从 5.0 开始 |
| `V_LINE_PWM` | 直线基础 PWM | 500 ≈ 50%，先低后高 |
| `V_ARC_PWM` | 弧线基础 PWM | 350，弯道降速保平稳 |
| `V_PWM_RAMP_PER_TICK` | 起步加速度 | 30 = 每秒 PWM 涨 3000，足够防滑 |
| `IR_ARC_END_TICKS` | 弧线退出全白确认次数 | 10 = 200ms |

## 反模式

❌ 在 `.c` 里硬编码 PID 参数（必须放 `config.h`）  
❌ 直接调用 HAL（这是 MSPM0，**用 driverlib**）  
❌ 不做 IMU 标定就启动（直线必偏）  
❌ 用 main 循环做 PID（必须 10ms 中断节拍）  
❌ 起步直接给最大 PWM（一定打滑，编码器虚高）

## 兜底方案

- DMP 不工作 → 自己用陀螺积分（已留接口）
- 编码器中断卡 → 改用 GPIO 中断 + 方向引脚的纯软件方案
- 红外阈值不稳 → 改用模拟版 + 软件滞回
- 工具链卡死 → 改用 IAR for ARM（同样支持 MSPM0）
