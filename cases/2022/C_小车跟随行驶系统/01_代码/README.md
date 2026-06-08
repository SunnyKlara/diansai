# 2022 C 小车跟随行驶系统 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c                    # 通过 -DCAR_LEAD / -DCAR_FOLLOW 二选一编译
├── Drivers/
│   ├── motor_msp.{c,h}            # TB6612 (MSP432 PWM)
│   ├── ir_line.{c,h}              # 5 路 IR
│   ├── ultrasonic.{c,h}           # HC-SR04 + 滤波
│   └── nrf_link.{c,h}             # NRF24L01 心跳
└── Algorithm/
    ├── line_pid.{c,h}             # 循迹 PD
    └── dist_pid.{c,h}             # 间距 PID（仅跟随车）
```

## 编译
- TI Code Composer Studio + MSP432P401R LaunchPad
- 领头车：`-DCAR_LEAD`；跟随车：`-DCAR_FOLLOW`
- TB6612: PWM 4 通道（TA0/TA1）
- HC-SR04: TRIG/ECHO（GPIO + Timer Capture）
- NRF24L01: SPI

## 调试节奏
1. 单车循迹（领头）→ 调 LINE_KP/LINE_KD
2. 跟随车开间距环（领头静止 20cm 处）→ 调 DIST_KP
3. 双车低速 0.3 m/s 跟随 → 看间距波动
4. 等停标识识别
5. 加速到 1.0 m/s 调试
