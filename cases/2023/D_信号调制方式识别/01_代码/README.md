# 2023 D 信号调制方式识别 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/rf_input.{c,h}
└── Algorithm/modulation_detect.{c,h}
```

## 编译
- STM32F407, 168MHz
- ADC1+ADC2 同步采 IQ
- 200 kSPS

## 测试
- 信号发生器输出 5 种调制 → 串口看类型识别
- 改 SNR 看识别率
