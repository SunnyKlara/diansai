# Boost升压电路 —— 从原理到实物

## 一、电路原理

```
Vin ──[电感L]──┬──[二极管D]──┬── Vout
               │             │
            [MOSFET Q]    [电容C]
               │             │
              GND ───────────┘
```

- Q导通：Vin给L充电，电感电流上升
- Q关断：电感通过D给C充电，Vout > Vin
- 输出电压：Vout = Vin / (1-D)

## 二、设计实例：5V→12V/1A

| 参数 | 值 |
|------|-----|
| Vin | 5V |
| Vout | 12V |
| Iout | 1A |
| fsw | 100kHz |
| D | 1 - 5/12 = 0.583 |

### 计算
- L = Vin × D / (ΔIL × fsw) = 5 × 0.583 / (0.5 × 100k) = 58μH → 选68μH
- C = Iout × D / (ΔVout × fsw) = 1 × 0.583 / (0.12 × 100k) = 48.6μF → 选100μF

### 器件选型
| 器件 | 型号 | 说明 |
|------|------|------|
| MOSFET | IRF3205 | Vds>12V即可 |
| 二极管 | SS34 | 肖特基，正向压降低 |
| 电感 | 68μH/3A | 工字电感 |
| 输出电容 | 100μF/25V | 电解+陶瓷并联 |

## 三、代码（与Buck类似，只是占空比计算不同）

```c
// Boost闭环控制
void boost_control_loop(void) {
    float vout = read_vout();
    float error = VOUT_REF - vout;  // 目标12V
    
    static float integral = 0;
    integral += error * Ki;
    integral = CLAMP(integral, 0.1, 0.85);
    
    float duty = Kp * error + integral;
    duty = CLAMP(duty, 0.1, 0.85);  // Boost占空比不能太大(≤0.85)
    
    set_pwm_duty(duty);
}
```

**注意**：Boost的占空比不能接近1（否则电感电流无穷大），通常限制在0.85以下。

## 四、Boost在电赛中的应用
- 2021C三端口DC-DC：电池放电时Boost升压
- 无人机电池升压供电
- 小信号放大器的电源
