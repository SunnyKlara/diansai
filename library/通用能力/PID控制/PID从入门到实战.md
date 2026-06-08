# PID控制 —— 从入门到电赛实战

> PID是电赛中使用频率最高的控制算法，几乎所有方向都会用到

## 一、原理（3分钟看懂）

```
设定值(r) ──→ [+] ──→ [PID控制器] ──→ [被控对象] ──→ 输出(y)
               ↑ -                                      │
               └──────────────────────────────────────────┘
                              反馈
```

- **P（比例）**：误差越大，输出越大。响应快但有稳态误差
- **I（积分）**：累积误差，消除稳态误差。但会引起超调和振荡
- **D（微分）**：预测误差变化趋势，抑制振荡。但对噪声敏感

### 公式
```
u(t) = Kp × e(t) + Ki × ∫e(t)dt + Kd × de(t)/dt
```

### 离散化（MCU实现用这个）
```
u[n] = Kp × e[n] + Ki × Σe[k] × Δt + Kd × (e[n] - e[n-1]) / Δt
```

## 二、C代码实现

### 位置式PID（最常用）
```c
typedef struct {
    float Kp, Ki, Kd;
    float integral;
    float prev_error;
    float output_max, output_min;
    float integral_max;  // 积分限幅，防止积分饱和
} PID_t;

void PID_Init(PID_t* pid, float kp, float ki, float kd,
              float out_min, float out_max) {
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->integral = 0;
    pid->prev_error = 0;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->integral_max = out_max * 0.8f;  // 积分限幅为输出的80%
}

float PID_Compute(PID_t* pid, float setpoint, float measurement) {
    float error = setpoint - measurement;
    
    // 积分项（带限幅）
    pid->integral += error;
    if (pid->integral > pid->integral_max) pid->integral = pid->integral_max;
    if (pid->integral < -pid->integral_max) pid->integral = -pid->integral_max;
    
    // 微分项
    float derivative = error - pid->prev_error;
    pid->prev_error = error;
    
    // PID输出
    float output = pid->Kp * error 
                 + pid->Ki * pid->integral 
                 + pid->Kd * derivative;
    
    // 输出限幅
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;
    
    return output;
}
```

### 使用示例
```c
// 电压稳压环
PID_t voltage_pid;
PID_Init(&voltage_pid, 0.5, 0.01, 0.1, 0.0, 0.95);

// 在定时器中断中调用
void control_loop(void) {
    float vout = read_voltage();  // 读ADC
    float duty = PID_Compute(&voltage_pid, 24.0, vout);  // 目标24V
    set_pwm_duty(duty);  // 设置PWM占空比
}
```

## 三、调参方法（实战经验）

### 手动调参步骤
1. **先只用P**：Ki=0, Kd=0
   - 从小到大增加Kp
   - 直到输出开始振荡
   - 记录此时Kp为Ku，振荡周期为Tu
   
2. **加入I**：
   - Kp = 0.6 × Ku
   - Ki = Kp / (0.5 × Tu)
   - 观察稳态误差是否消除
   
3. **加入D**（如果需要）：
   - Kd = Kp × 0.125 × Tu
   - 观察是否抑制了超调

### 各方向典型参数范围

| 应用场景 | Kp | Ki | Kd | 控制周期 |
|----------|-----|-----|-----|---------|
| Buck稳压 | 0.1~1.0 | 0.001~0.1 | 0~0.5 | 50μs |
| 逆变器电压环 | 0.5~5.0 | 0.01~0.5 | 0~1.0 | 100μs |
| 电机转速 | 1.0~10.0 | 0.1~1.0 | 0~0.5 | 1ms |
| 小车循迹 | 5~50 | 0.01~1.0 | 1~20 | 10ms |
| 云台角度 | 0.5~5.0 | 0.01~0.5 | 0.1~2.0 | 20ms |

## 四、电赛中PID的典型应用

| 方向 | 被控量 | 控制量 | 注意事项 |
|------|--------|--------|----------|
| 电源 | 输出电压 | PWM占空比 | 双闭环（电压环+电流环） |
| 智能车 | 车身偏移 | 舵机角度/差速 | 微分项很重要 |
| 无人机 | 姿态角 | 电机转速 | 串级PID（角速度+角度+位置） |
| 云台 | 激光位置 | 舵机角度 | 需要快速响应 |
| 磁悬浮 | 悬浮高度 | 电磁铁电流 | 系统不稳定，PD为主 |

## 五、常见坑

1. **积分饱和**：长时间误差累积导致积分项过大→必须加积分限幅
2. **微分噪声**：ADC噪声被微分放大→对测量值做低通滤波后再微分
3. **采样周期不一致**：定时器中断被打断→用硬件定时器保证周期
4. **量纲不匹配**：Kp的单位是"输出/误差"，注意物理量纲
