# 2023 C 电感电容测量装置 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/
│   ├── dds_ad9833.{c,h}
│   └── adc_dual.{c,h}
└── Algorithm/
    └── impedance.{c,h}
```

## 编译
- STM32F103C8T6 72MHz
- SPI1 → AD9833
- ADC1+ADC2 同步 + DMA
- OLED I2C
