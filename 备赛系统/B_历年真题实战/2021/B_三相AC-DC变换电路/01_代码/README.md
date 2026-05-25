# 2021 B 三相 AC-DC 变换电路 —— 工程编译说明

## 目录
```
01_代码/
├── config.h
├── Core/main.c                    # ST_IDLE → PRECHARGE → RUN → FAULT
├── Drivers/
│   ├── adc_threephase.{c,h}       # 9 通道同步采样
│   ├── pwm_vienna.{c,h}           # 三路 Vienna PWM + Buck PWM
│   └── oled.{c,h}
└── Algorithm/
    ├── park_clarke.{c,h}          # Clarke/Park + SRF-PLL
    ├── vienna_dq.{c,h}            # Vienna DQ 双闭环
    └── buck_loop.{c,h}            # Buck 双环
```

## 编译
- MCU：STM32G474RET6（170MHz）
- 工具：CubeMX + Keil/CubeIDE
- 替换 `_hal_xxx` 占位为 HAL 调用即可

## 调试节奏
1. 单台 Vienna 空载 PWM（120V 输入低压验证）
2. 加 PLL 看 θ 是否锁定到 Va 相位
3. 加电流环 → 闭母线电压环
4. 接 Buck 后级 → 测 Vout 36V
5. 满载 2A 测效率与 PF
