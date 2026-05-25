# 2021 G 植保飞行器 —— 工程编译说明

## 目录（仅任务 MCU；飞控用 PX4 固件）
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── openmv_uart.{c,h}
│   ├── lora_module.{c,h}
│   └── pump_pwm.{c,h}
└── Algorithm/spray_planner.{c,h}
```

## 编译
- 飞控：Cube Orange + PX4 firmware（不在本工程范围）
- 任务 MCU：STM32F407, OpenMV via UART, LoRa via UART2, 蠕动泵 GPIO

## OpenMV 输出协议
[0xAA][color][x_hi][x_lo][y_hi][y_lo][crc]
