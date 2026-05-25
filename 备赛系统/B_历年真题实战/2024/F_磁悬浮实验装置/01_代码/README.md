# 2024 F 磁悬浮实验装置 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── hall_sensor.{c,h}        # SS49E + 距离查表
│   └── pwm_coil.{c,h}           # 5 路 PWM
└── Algorithm/
    └── maglev_pd.{c,h}          # PD 微分先行
```

## 编译
- STM32F407VET6 168MHz
- TIM1 + TIM8 共 5 通道 PWM 20kHz（E0~E4 各 1）
- ADC1 + DMA 同步采 5 路霍尔
- 控制 ISR 2kHz（基础定时 TIM6）

## 调试节奏
1. 单电磁铁先试 Kp=5, Kd=0.5 看是否能维持悬浮 1s
2. 加 D 项调到 Kd=0.8 抑制振荡
3. 慢加 Ki=0.1 消除稳态误差
4. 加四周电磁铁 → 提高水平稳定
5. 加载 20g 看是否仍稳
6. 改设定高度 + ramp → 看动态响应
