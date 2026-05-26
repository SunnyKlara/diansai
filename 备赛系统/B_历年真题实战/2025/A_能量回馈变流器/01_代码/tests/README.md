# 2025 A 能量回馈变流器 — 算法层 PC 验证

`algo_reference.py` 是 SVPWM + RMS + 电压外环 + 同步整流的 Python 等价实现。

```bash
python algo_reference.py
```

## 验证矩阵

### ✅ SVPWM 七段式

| 检查 | 期望 | 实测 | 评价 |
|---|---|---|---|
| 三相占空比平均 | (0.5, 0.5, 0.5) | (0.500, 0.500, 0.500) | ✓ 完美对称 |
| 6 扇区分布 | 每扇区 ~67 次 | 66~67 次 | ✓ 均匀 |
| 线电压 RMS（m=0.8, Vdc=48V） | m×Vdc/√2 = 27.153V | 27.153V | ✓ **精度 100%** |

### ✅ RMS 滑动窗口

| 检查 | 实测 | 评价 |
|---|---|---|
| 32V RMS 正弦输入 | 31.838V | -0.162V，容差 ±0.25V 内 ✓ |

### ⚠️ 电压外环（暴露关键工程边界）

仿真**有意暴露了一个重要工程问题**：

```
理论最大 Vline_RMS = m_max × Vdc / √2 = 0.95 × 48 / √2 = 32.24V
减去 3% 内阻压降                       ≈ 31.27V
PI 无论怎么调，物理上达不到 32V 设定值！
```

**这是 2025 A 题的关键陷阱**！
若现场出现"PI 调到饱和但 Vline RMS 还差 0.5~1V"，根因是**母线电压不够**，而非控制参数。

**对策（按优先级）：**

1. **首选**：直流源开到 50V（题目允许 ±10% 即 43.2~52.8V），物理上限提到 33.59V，留 1.5V 余量
2. **备选**：把 `VLOOP_OUT_MAX = 0.95` 提到 0.98（不推荐，过调制会引入低次谐波）
3. **绝不**：盲目调大 Ki —— 撞顶后增大 Ki 只会加剧饱和

## 同步整流状态（基于电流过零）

| ia 输入 | 状态 |
|---|---|
| +2.00 A | HIGH_ON ✓ |
| +0.05 A | OFF ✓ |
| -0.05 A | OFF ✓ |
| -1.50 A | LOW_ON ✓ |

## 与 C 代码的对应关系

| Python | C 文件 |
|---|---|
| `svpwm_calc()` | `Algorithm/svpwm_3phase.c::SVPWM_Calc()` |
| `RMSMeter` | `Algorithm/rms_meter.c::RMSMeter` |
| `VoltageLoop.update()` | `Algorithm/voltage_loop.c::VLoop_Update()` |
| `sr_state()` | `Algorithm/feedback_control.c::FB_Update()` 中的相状态 |

修改 `config.h` 后必须同步修改本脚本顶部常量。
