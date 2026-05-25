# 2024 E 三子棋游戏装置 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── xy_gantry.{c,h}        # 步进 + 限位 + 电磁铁
│   └── openmv_board.{c,h}     # OpenMV → MCU 棋盘协议（10 字节包）
└── Algorithm/
    └── minimax.{c,h}          # α-β Minimax
```

## 编译
- STM32F407 168MHz
- TIM2/TIM3 步进 PUL/DIR
- USART → OpenMV (115200)
- GPIO → 电磁铁 + 限位

## OpenMV 协议
```
[0xAA][b0][b1]..[b8][CRC]   总 11 字节
b_i ∈ {0=空, 1=装置（黑）, 2=对手（白）}
CRC = b0 ^ b1 ^ ... ^ b8
```
