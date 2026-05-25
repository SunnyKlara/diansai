# TI MSPM0 电赛快速入门

> 越来越多题目指定TI MCU，MSPM0是必须掌握的平台

## 一、为什么要学MSPM0

- 2024H自动行驶小车：**指定MSPM0**
- 2025E自行瞄准装置：**指定MSPM0控制循迹和电机**
- 未来趋势：TI杯和部分赛区赛会继续指定

## 二、MSPM0 vs STM32 对比

| 对比项 | MSPM0G3507 | STM32F103C8T6 |
|--------|------------|---------------|
| 内核 | ARM Cortex-M0+ | ARM Cortex-M3 |
| 主频 | 80MHz | 72MHz |
| Flash | 128KB | 64KB |
| RAM | 32KB | 20KB |
| ADC | 12bit 4MSPS | 12bit 1MSPS |
| 定时器 | TIMA/TIMG | TIM1~TIM4 |
| 开发环境 | CCS + SysConfig | Keil/CubeIDE + CubeMX |
| 价格 | LaunchPad ¥60 | 最小系统板 ¥10 |

**主要区别**：开发工具和外设配置方式不同，核心编程思路相似。

## 三、开发环境搭建

### 1. 安装CCS (Code Composer Studio)
- 下载：ti.com/tool/CCSTUDIO
- 安装时选择"MSPM0"支持
- 内置SysConfig配置工具

### 2. 安装MSPM0 SDK
- 下载：ti.com/tool/MSPM0-SDK
- 包含驱动库、示例代码、文档

### 3. LaunchPad连接
- USB连接即可（自带XDS110调试器）
- 不需要额外的ST-Link/J-Link

## 四、SysConfig配置（相当于CubeMX）

SysConfig是TI的图形化配置工具，功能类似STM32的CubeMX。

### GPIO配置
```
SysConfig → GPIO → 添加引脚
  Direction: Output / Input
  Pull: Pull-Up / Pull-Down / None
  
生成的代码：
  DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_7);      // 输出高
  DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_7);    // 输出低
  DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_0);     // 读输入
```

### PWM配置
```
SysConfig → TIMER → 添加TIMA0
  Mode: PWM
  Period: 1000 (对应频率)
  Channels: CH0, CH1, CH2, CH3
  
生成的代码：
  DL_TimerA_startCounter(TIMA0);
  DL_TimerA_setCaptureCompareValue(TIMA0, 500, DL_TIMER_CC_0_INDEX);
```

### ADC配置
```
SysConfig → ADC12 → 添加ADC0
  Resolution: 12-bit
  Sample Time: 根据需要
  Trigger: Software / Timer
```

## 五、从STM32迁移到MSPM0的速查表

| STM32 HAL | MSPM0 DriverLib | 说明 |
|-----------|-----------------|------|
| HAL_GPIO_WritePin(port, pin, SET) | DL_GPIO_setPins(port, pin) | 输出高 |
| HAL_GPIO_WritePin(port, pin, RESET) | DL_GPIO_clearPins(port, pin) | 输出低 |
| HAL_GPIO_ReadPin(port, pin) | DL_GPIO_readPins(port, pin) | 读输入 |
| __HAL_TIM_SET_COMPARE(htim, ch, val) | DL_TimerA_setCaptureCompareValue(tim, val, idx) | 设PWM |
| HAL_TIM_PWM_Start(htim, ch) | DL_TimerA_startCounter(tim) | 启动PWM |
| HAL_Delay(ms) | delay_cycles(ms * 80000) | 延时(80MHz) |
| HAL_ADC_Start/GetValue | DL_ADC12_startConversion / getMemResult | ADC |

## 六、MSPM0最小工程模板

```c
#include "ti_msp_dl_config.h"  // SysConfig生成

int main(void)
{
    SYSCFG_DL_init();  // SysConfig生成的初始化函数
    
    // LED闪烁测试
    while (1) {
        DL_GPIO_togglePins(GPIOA, DL_GPIO_PIN_0);
        delay_cycles(40000000);  // 0.5s @ 80MHz
    }
}
```

## 七、电赛中MSPM0的典型用法

### 循迹小车（2024H/2025E）
```c
// 读传感器 → PID计算 → 电机输出
void control_loop(void) {
    uint8_t s1 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_12) ? 1 : 0;
    // ... 读其他传感器
    
    float error = calc_error(s1, s2, s3, s4, s5);
    float correction = pid_compute(error);
    
    int left = BASE_SPEED - (int)correction;
    int right = BASE_SPEED + (int)correction;
    
    DL_TimerA_setCaptureCompareValue(TIMA0, left*10, DL_TIMER_CC_0_INDEX);
    DL_TimerA_setCaptureCompareValue(TIMA0, right*10, DL_TIMER_CC_2_INDEX);
}
```

## 八、常见坑

1. **delay_cycles不是毫秒！** 参数是CPU周期数，80MHz时1ms=80000
2. **GPIO读取返回的是引脚掩码**，不是0/1，需要判断是否非零
3. **SysConfig每次修改后会重新生成代码**，不要手动改生成的文件
4. **LaunchPad上的LED和按键引脚**因型号而异，查原理图确认
