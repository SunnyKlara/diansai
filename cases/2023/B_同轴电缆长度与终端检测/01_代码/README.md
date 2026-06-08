# 2023 B 同轴电缆长度与终端检测 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/tdr_capture.{c,h}
└── Algorithm/tdr_analyze.{c,h}
```

## 编译
- STM32F407 168MHz
- TIM1 阶跃 + 等效采样 ADC
- OLED 显示

## 调试
1. 先用已知 1m 同轴 + 50Ω 终端 → 阻抗读 50Ω, 长度 1m
2. 换 100Ω → 反射系数 +0.33, 长度不变
3. 换开路 → 反射 +1.0
4. 换短路 → 反射 -1.0
