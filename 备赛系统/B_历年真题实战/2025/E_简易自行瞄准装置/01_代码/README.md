# 2025E 代码工程说明

## 双MCU架构

本题要求MSPM0控制循迹和电机，瞄准模块用独立MCU。两套代码完全独立，各自有独立电源开关。

```
01_代码/
├── 小车循迹/
│   ├── main_car.c          MSPM0G3507主程序
│   └── (SysConfig生成的外设配置文件)
│
├── 瞄准模块/
│   ├── main_aim.c          STM32F103主程序（云台+激光控制）
│   └── openmv_target_detect.py   OpenMV靶标检测脚本
│
└── README.md               本文件
```

## 小车循迹部分（MSPM0G3507）

### 开发环境
- IDE: Code Composer Studio (CCS) 12.x
- 配置工具: SysConfig
- SDK: MSPM0 SDK 2.x

### 引脚分配
| 引脚 | 功能 | 说明 |
|------|------|------|
| PA12~PA16 | GPIO_IN | 5路红外传感器 |
| PA0/PA1 | TIMA0_C0/C1 | 左电机PWM(正/反) |
| PA2/PA3 | TIMA0_C2/C3 | 右电机PWM(正/反) |
| PB0/PB1 | TIMG0 | 左编码器AB相 |
| PB2/PB3 | TIMG1 | 右编码器AB相 |
| PA6 | GPIO_OUT | 蜂鸣器 |
| PA7 | GPIO_OUT | LED指示灯 |
| PB8 | GPIO_IN | 启动按键(上拉) |

### 编译步骤
1. 打开CCS，导入MSPM0工程
2. 用SysConfig配置上述引脚
3. 将main_car.c替换生成的main.c
4. 编译下载到LaunchPad

## 瞄准模块部分（STM32F103 + OpenMV）

### STM32引脚分配
| 引脚 | 功能 | 说明 |
|------|------|------|
| PA1 | TIM2_CH2 | 偏航舵机PWM(50Hz) |
| PA2 | TIM2_CH3 | 俯仰舵机PWM(50Hz) |
| PA9/PA10 | USART1 | 连接OpenMV串口 |
| PB0 | GPIO_OUT | 激光笔开关(高=开) |
| PC13 | GPIO_IN | 启动按键 |

### OpenMV连接
| OpenMV | STM32 |
|--------|-------|
| P4(TX) | PA10(RX) |
| P5(RX) | PA9(TX) |
| GND | GND |
| VIN | 5V |

### 通信协议
OpenMV → STM32，115200bps，格式：`$cx,cy,found\n`
- cx: 靶心X偏移(像素，右为正)
- cy: 靶心Y偏移(像素，下为正)
- found: 1=检测到, 0=未检测到
