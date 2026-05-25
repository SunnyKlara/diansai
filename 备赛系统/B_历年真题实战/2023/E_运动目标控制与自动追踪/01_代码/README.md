# 2023 E 运动目标控制与自动追踪 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c                    # -DLASER_RED / -DLASER_GREEN
├── Drivers/
│   ├── servo_gimbal.{c,h}         # MG996R 50Hz PWM
│   └── openmv_proto.{c,h}         # OpenMV → MCU UART 协议
└── Algorithm/
    ├── path_gen.{c,h}             # 边线 / A4 路径生成
    └── track_pid.{c,h}             # 追踪 PID
```

## 编译
- STM32F103C8T6 + Keil/CubeIDE
- TIM2 50Hz PWM × 4 通道（每云台 2 路）
- USART1 (115200) ← OpenMV
- 红色 MCU：`-DLASER_RED`；绿色 MCU：默认（不定义）

## OpenMV 端代码（参考）
```python
import sensor, image
from pyb import UART
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QQVGA)  # 160x120
uart = UART(3, 115200)
RED = (30,80, 40,80, 10,60)

while True:
    img = sensor.snapshot()
    blobs = img.find_blobs([RED], pixels_threshold=5)
    if blobs:
        b = max(blobs, key=lambda b: b.pixels())
        cx, cy = b.cx(), b.cy()
        crc = ((cx>>8)&0xff) ^ (cx&0xff) ^ ((cy>>8)&0xff) ^ (cy&0xff)
        uart.write(bytes([0xAA, (cx>>8)&0xff, cx&0xff,
                          (cy>>8)&0xff, cy&0xff, crc&0xff]))
```
