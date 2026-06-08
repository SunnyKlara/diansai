# 2021 F 智能送药小车 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── motor.{c,h}        # TB6612
│   ├── encoder.{c,h}      # JGA25-370 编码器
│   ├── ir_line.{c,h}      # 5 路红外
│   ├── openmv_uart.{c,h}  # OpenMV → STM32 UART
│   └── nrf24.{c,h}        # 双车通信
└── Algorithm/
    ├── line_pid.{c,h}     # 循迹 PD
    └── path_planner.{c,h} # 节点 → 动作
```

## 编译
- STM32F103C8T6, 72MHz, Keil/CubeIDE
- TB6612 → PWM 4 通道
- 5 路 IR → 5 通道 ADC
- OpenMV UART2 (115200)
- NRF24L01 SPI1

## OpenMV 端代码（参考）
```python
import sensor, image, time
from pyb import UART
sensor.reset(); sensor.set_pixformat(sensor.GRAYSCALE)
sensor.set_framesize(sensor.QQVGA)
uart = UART(3, 115200)
clock = time.clock()
# Haar 分类器载入数字 1..8
while True:
    clock.tick()
    img = sensor.snapshot()
    # ... 数字检测
    uart.write(bytes([detected_digit + ord('0')]))
```
