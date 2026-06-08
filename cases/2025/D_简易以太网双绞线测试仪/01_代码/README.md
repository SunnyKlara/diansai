# 2025 D 简易以太网双绞线测试仪 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c
├── Drivers/pair_mux.{c,h}        # CD4051 4 对线切换
└── Algorithm/twisted_pair.{c,h}  # 4 对 TDR 分析
```

## 复用
TDR 算法直接复用 2023 B 同款（同名函数）。仅需：
- Z₀ 改为 100Ω
- Vp 改为 1.95e8 m/s
- 4 路 mux 切换

## 编译
- STM32F407
- 4 路时分复用 → 单帧 TDR 100ms
