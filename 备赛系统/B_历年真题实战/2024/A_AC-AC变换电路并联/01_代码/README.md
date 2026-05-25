# 2024 A AC-AC 变换电路并联 —— 工程编译说明

## 目录
```
01_代码/
├── config.h                      # 全局参数
├── Core/main.c                   # 状态机 + 中断挂接
├── Drivers/                      # 板级（HAL）
│   ├── pwm_chopper.{c,h}         # HRTIM 4 通道 + 四步换流
│   ├── zero_cross.{c,h}          # PC817 光耦过零检测
│   ├── adc_vio.{c,h}             # 双 ADC：Vin/Vout/Iout + RMS
│   └── oled.{c,h}                # 0.96" SSD1306
└── Algorithm/                    # 与硬件解耦
    ├── ac_chopper.{c,h}          # 占空比 + 四步换流序列
    └── voltage_loop.{c,h}        # Vrms PI + 下垂均流
```

## 编译

### 真机 STM32CubeIDE / Keil
1. CubeMX 选 STM32G474RET6, HSE 24MHz → SYSCLK 170MHz
2. HRTIM1: TIMA/TIMB 各两通道, fsw = 20kHz, 中心对齐
3. ADC1+ADC2: 同步双通道, HRTIM TRGO 触发, DMA1
4. EXTI: PC817 输出 → PA11 上升沿 + 下降沿
5. I2C1: PB6/PB7 → OLED
6. 替换 Drivers/*.c 中 `_hal_xxx` 占位为 HAL 调用

### PC 端验证
```
gcc Algorithm/*.c -I . -lm -o /tmp/test
```

## 中断挂接

```c
void HRTIM1_TIMA_IRQHandler(void){ main_pwm_isr(); }
void EXTI15_10_IRQHandler(void) { zcd_on_edge_isr(get_us()); main_zcd_isr(); }
void SysTick_Handler(void)      { HAL_IncTick(); main_systick_isr(); }
```

## 调试节奏

1. 单台开环：固定 D=0.5，看 Vout ≈ 18V
2. 加 ZCD 中断 + 四步换流，慢速 50Hz 验证波形
3. 关闭一台 → 单台闭环 PI（先 Kp=0.01, Ki=0），稳后加 Ki
4. 两台并联（共享输入输出，独立 MCU）→ 测均流系数 K
5. 改一台 V_set 偏移 → K 可调

## 已知 TODO
- [ ] HRTIM 替换为真实 ARR 计算
- [ ] 过零中断滤波（光耦抖动）
- [ ] 重量 < 600g 实测
