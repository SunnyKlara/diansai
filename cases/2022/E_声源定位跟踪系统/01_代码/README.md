# 2022 E 声源定位跟踪系统 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── mic_array.{c,h}        # 4 路同步 ADC + DMA
│   └── servo_pan_tilt.{c,h}   # 二轴 MG996R
└── Algorithm/tdoa.{c,h}       # 互相关 + 几何
```
