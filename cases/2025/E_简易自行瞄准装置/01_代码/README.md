# 2025 E 简易自行瞄准装置 —— 工程编译说明

## 双 MCU 架构
```
01_代码/
├── config.h                       # 共享参数（小车 + 瞄准）
├── Core/
│   ├── main_car.c                 # 小车 MCU 入口（STM32F407）
│   └── main_aim.c                 # 瞄准 MCU 入口（STM32F103）
├── Drivers/
│   ├── motor.{c,h}                # 小车 TB6612
│   ├── ir_line.{c,h}              # 小车 5 路 IR
│   ├── servo_aim.{c,h}            # 瞄准 MG996R 二轴云台
│   ├── openmv_in.{c,h}            # 瞄准 OpenMV UART 协议
│   └── uart_link.{c,h}            # 双 MCU 间通信
└── Algorithm/
    ├── line_pid.{c,h}             # 小车循迹 PD
    └── aim_pid.{c,h}              # 瞄准 PD
```

## 编译
- 小车 MCU：STM32F407VET6, 入口 `Core/main_car.c`
- 瞄准 MCU：STM32F103C8T6, 入口 `Core/main_aim.c`
- 共用 `config.h`（参数集中）
- 通信：UART2 单向 1 字节信令

## OpenMV 端代码（在瞄准 MCU 旁，外置）
靶心检测：颜色阈值 + find_blobs，最大块即靶心。
