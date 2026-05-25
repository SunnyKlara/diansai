# 2025A 代码工程说明

## 开发环境
- IDE: STM32CubeIDE 1.13+
- MCU: STM32G474RET6 (170MHz)
- HAL库 + CMSIS-DSP

## 文件结构
```
01_代码/
├── main.c              主程序（初始化、主循环、中断、按键、显示）
├── svpwm_3phase.c/.h   SVPWM算法（扇区判断、矢量时间、七段式分配）
└── README.md           本文件
```

## 引脚分配

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA8/PA7 | TIM1_CH1/CH1N | A相桥臂（互补+死区） |
| PA9/PB0 | TIM1_CH2/CH2N | B相桥臂 |
| PA10/PB1 | TIM1_CH3/CH3N | C相桥臂 |
| PA0 | ADC1_IN1 | A相电压采样 |
| PA1 | ADC1_IN2 | B相电压采样 |
| PA2 | ADC1_IN3 | C相电压采样 |
| PA3 | ADC2_IN4 | A相电流采样 |
| PB6/PB7 | I2C1 | OLED |
| PC0~PC3 | GPIO_IN | 按键(频率+/-/启动/停止) |

## TIM1配置
- 中心对齐模式（减少谐波）
- PSC=0, ARR=4249 → 170MHz/2/4250 = 20kHz
- 死区: DTG=136 → 800ns
- 三通道互补输出
- 使能更新中断

## 频率调节方法
- 不改变开关频率(固定20kHz)
- 通过改变SVPWM中theta的递增速度改变输出频率
- omega = 2π × freq → theta += omega × dt
- dt = 1/20000 = 50μs

## 与2023A代码的区别
| 对比 | 2023A | 2025A |
|------|-------|-------|
| 相数 | 单相(2路PWM) | 三相(6路PWM) |
| 调制 | SPWM(正弦表) | SVPWM(矢量计算) |
| 频率 | 固定50Hz | 可调20~100Hz |
| 电压采样 | 单相RMS | 三相线电压RMS |
