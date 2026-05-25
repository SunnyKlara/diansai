# 2021 J 周期信号波形识别 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── adc_capture.{c,h}        # ADC + DMA + 1024 点帧
│   └── range_switch.{c,h}       # CD4053 量程切换
└── Algorithm/
    ├── waveform_id.{c,h}        # FFT + H3/H1 阈值
    └── param_meas.{c,h}         # 频率/Vpp/占空比
```

## 编译
- STM32F407VET6 168MHz
- ADC1 + DMA1, 多档采样率（2k/100k/500k SPS）
- TIM2 触发 ADC
- I2C1 → OLED
- GPIO 控 CD4053（3 通道选择）

## 调试节奏
1. 信号发生器送 1V 1kHz 正弦 → 串口看 ADC 读数是否 ≈ 1V
2. FFT 输出 50Hz~50kHz 范围内 1kHz bin 是否最大
3. 切到三角波 → H3/H1 应 ≈ 0.11
4. 切到矩形波 → H3/H1 应 ≈ 0.33
5. 切电压档：50mV/200mV/2V/10V → 自动量程
6. 测频精度：误差 < 1%
