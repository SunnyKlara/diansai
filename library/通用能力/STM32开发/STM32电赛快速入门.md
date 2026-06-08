# STM32电赛快速入门

> 不讲原理，只讲电赛中最常用的功能怎么快速用起来

## 一、开发环境搭建（30分钟搞定）

### 方案A：STM32CubeIDE（推荐，免费）
1. 下载安装 STM32CubeIDE（st.com）
2. 内置CubeMX图形化配置
3. 内置编译器和调试器
4. 支持ST-Link调试

### 方案B：Keil MDK（传统方案）
1. 安装Keil MDK 5.38+
2. 安装STM32对应的Pack
3. 需要单独安装CubeMX
4. 需要注册（有免费版限制32KB）

### 下载器
- ST-Link V2（¥15，最常用）
- J-Link（¥50+，更快更稳）
- 接线：SWDIO、SWCLK、GND、3.3V

## 二、CubeMX配置速查

### GPIO输出（控制LED/蜂鸣器/继电器）
```
Pinout → 点击引脚 → GPIO_Output
配置：Push-Pull, No Pull, Low速度
代码：
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);    // 高电平
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);  // 低电平
  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);                  // 翻转
```

### GPIO输入（读按键/传感器）
```
Pinout → 点击引脚 → GPIO_Input
配置：Pull-Up（按键接地时用上拉）
代码：
  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET) {
      // 按键按下
  }
```

### 定时器PWM（电机调速/舵机控制）
```
Pinout → TIMx_CHx → PWM Generation
配置：
  PSC = 71 (72MHz/72 = 1MHz计数频率)
  ARR = 999 (1MHz/1000 = 1kHz PWM频率)
  
代码：
  HAL_TIM_PWM_Start(&htimx, TIM_CHANNEL_x);
  __HAL_TIM_SET_COMPARE(&htimx, TIM_CHANNEL_x, 500);  // 50%占空比
```

### 舵机控制（50Hz PWM）
```
PSC = 71, ARR = 19999 → 50Hz
脉宽500~2500μs对应0~180度

  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 1500);  // 90度(中位)
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 500);   // 0度
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 2500);  // 180度
```

### ADC（电压采样）
```
Pinout → PAx → ADCx_INx
配置：12bit, 单次转换或连续+DMA
代码见 "ADC采样与信号调理.md"
```

### UART串口（调试/通信）
```
Pinout → PA9/PA10 → USART1
配置：115200, 8N1
代码：
  // 发送
  char buf[64];
  sprintf(buf, "Vout=%.2fV\r\n", vout);
  HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
  
  // 接收（中断方式）
  uint8_t rx_byte;
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
```

### I2C（OLED显示）
```
Pinout → PB6/PB7 → I2C1
配置：400kHz Fast Mode
代码：见display.c
```

### 定时器中断（周期性任务）
```
Pinout → TIM3 → Internal Clock
配置：PSC=7199, ARR=99 → 100Hz(10ms)中断
代码：
  HAL_TIM_Base_Start_IT(&htim3);
  
  void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
      if (htim->Instance == TIM3) {
          // 每10ms执行一次
          control_loop();
      }
  }
```

## 三、电赛最常用的代码模式

### 模式1：主循环+定时器中断
```c
// 定时器中断做实时控制（PID、PWM更新）
// 主循环做非实时任务（显示、按键、通信）

int main(void) {
    // 初始化...
    HAL_TIM_Base_Start_IT(&htim3);  // 启动10ms定时中断
    
    while (1) {
        update_display();   // 更新显示
        check_buttons();    // 检查按键
        HAL_Delay(100);     // 100ms刷新
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        read_sensors();     // 读传感器
        pid_control();      // PID计算
        motor_output();     // 输出到电机
    }
}
```

### 模式2：ADC DMA + 处理回调
```c
// ADC通过DMA自动采集数据
// DMA完成后在回调中处理

HAL_ADC_Start_DMA(&hadc1, (uint32_t*)buf, 1024);

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    // 1024个采样点已就绪
    do_fft(buf, 1024);
    calc_thd();
}
```

## 四、调试技巧

1. **串口打印是最好的调试工具**：printf重定向到UART
2. **LED闪烁确认程序在跑**：主循环里翻转LED
3. **示波器看PWM波形**：确认频率和占空比
4. **断点调试看变量**：Keil/CubeIDE都支持
5. **逻辑分析仪看时序**：I2C/SPI通信调试
