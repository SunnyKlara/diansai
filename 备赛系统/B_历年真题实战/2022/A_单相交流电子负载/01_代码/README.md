# 2022 A 单相交流电子负载 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c                # 控制 ISR + 主循环
├── Drivers/
│   ├── adc_load.{c,h}         # v_grid / i_grid / Vbus / Vout
│   └── pwm_eload.{c,h}        # 前/后级 H 桥
└── Algorithm/
    ├── sogi_pll.{c,h}         # SOGI + DQ-PLL
    └── pf_control.{c,h}       # 1/4 周期延迟 β + dq 双 PI
```
- 删除：旧扁平 `pf_control.c`（已拆到 Algorithm + 移除 HAL 依赖）

## 编译
- STM32G474RE @ 170 MHz
- TIM1 中心对齐 PWM 20kHz, 死区 500ns
- 双 ADC 同步 + DMA：4 通道（V_grid, I_grid, Vbus, Vout）
- I2C1 → OLED

## 调试节奏
1. 仅开 PLL，串口打 θ → 验证锁相到 50Hz
2. 开前级 PWM，cosφ=1.0 R 模式 → 看 i 与 v 同相
3. 切到 cosφ=0.7 L 模式 → 看 i 滞后 v 45.6°
4. 同理 cosφ=0.7 C 模式 → 超前
5. 接后级 SPWM → 看母线稳态电压
6. 测 ΔP 优化
