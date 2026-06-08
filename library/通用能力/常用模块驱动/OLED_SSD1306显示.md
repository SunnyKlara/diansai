# OLED SSD1306显示 —— 电赛最常用的显示方案

## 一、模块参数
- 分辨率：128×64像素
- 接口：I2C（4线）或SPI（7线）
- 供电：3.3V~5V
- I2C地址：0x3C（或0x3D）

## 二、I2C接线
| OLED | STM32 |
|------|-------|
| VCC | 3.3V |
| GND | GND |
| SCL | PB6 (I2C1_SCL) |
| SDA | PB7 (I2C1_SDA) |

## 三、快速使用（基于现成库）

推荐使用u8g2库或自己移植的简化版。电赛中最常用的显示功能：

```c
// 显示字符串
OLED_ShowString(0, 0, "Hello NUEDC!");

// 显示浮点数（电赛最常用）
char buf[20];
sprintf(buf, "Vout:%.2fV", vout_rms);
OLED_ShowString(0, 2, buf);

// 显示整数
sprintf(buf, "Freq:%dHz", freq);
OLED_ShowString(0, 4, buf);

// 清屏
OLED_Clear();
```

## 四、电赛显示模板

```
┌──────────────────────┐
│ 2025A Inverter  RUN  │  ← 第0行：标题+状态
│                      │
│ Vout: 24.01V         │  ← 第2行：输出电压
│ Iout:  2.00A         │  ← 第4行：输出电流
│ Freq:    50Hz        │  ← 第6行：频率
│ Effi: 89.2%          │  ← 第8行：效率(如果有空间)
└──────────────────────┘
```

## 五、注意事项
- OLED功耗约20~40mW，低功耗题目(2024B)可能不适用
- I2C速度设400kHz，刷新率约10~20fps
- 不要在中断里调用OLED函数（I2C通信耗时）
- 主循环中每100ms刷新一次即可
