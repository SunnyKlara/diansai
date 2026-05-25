# 2022 D 混沌信号产生实验装置 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/dac_dual.{c,h}
└── Algorithm/chua_oscillator.{c,h}
```

## 编译
- STM32F407 168MHz
- TIM6 触发 DAC + DMA, 50 kSPS
- 启动后 X-Y 模式接示波器

## 多系统切换
按键切换：Chua / Lorenz / Rössler 三套参数（占位，扩展时增加 lorenz.c / rossler.c）。
