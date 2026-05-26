# 2021 A 信号失真度测量装置 — 算法层 PC 验证

`algo_reference.py` 是 **过零检测 + 同步采样 + FFT + THD** 的 Python 等价实现。

```bash
python algo_reference.py
```

## 算法核心

```
ADC 1024 点          先用 100kHz 粗扫
   ↓                 ↓
过零检测 → 频率      → 同步采样率 = freq × N / M
                                   ↓
                    高精度采样 1024 点
                                   ↓
                    （同步采样下用矩形窗 → 整数 bin → 0 泄漏）
                                   ↓
                    FFT → 找基波 bin → 取 H1~H5 → THD = √(ΣH²)/H1
```

## 验证结果（**全部通过 ✅**，远超题目精度要求）

### 频率检测精度

| 频率档 | 期望 | 实测 | 误差 |
|---|---|---|---|
| 1 kHz | 1000.00 Hz | 1000.00 Hz | 0.0000% |
| 10 kHz | 10000.00 Hz | 10000.00 Hz | 0.0000% |
| 50 kHz（fs=500k） | 50000.00 Hz | 50000.00 Hz | 0.0000% |

### THD 测量精度（1kHz 基波，同步采样）

| THDo | THDx | 绝对误差 | 基本 ≤5% | 发挥 ≤3% |
|---|---|---|---|---|
| 5.0% | 5.00% | +0.002 | ✓ | ✓ |
| 10.0% | 10.00% | +0.004 | ✓ | ✓ |
| 20.0% | 20.00% | +0.000 | ✓ | ✓ |
| 30.0% | 30.00% | +0.001 | ✓ | ✓ |
| 50.0% | 50.00% | +0.005 | ✓ | ✓ |

### THD 测量精度（100kHz 发挥部分，fs=1.024 MSPS）

| THDo | THDx | 误差 | 发挥 ≤3% |
|---|---|---|---|
| 10.0% | 10.00% | +0.004 | ✓ |
| 30.0% | 30.00% | +0.001 | ✓ |

**算法理论误差远低于发挥部分容差 3%，所有空间留给硬件链路（信号调理 + ADC ENOB）**。

## 关键设计决策

### 1. 同步采样下用矩形窗，非同步用平顶窗

- 同步采样：基波恰好在整数 bin → 矩形窗精度最高（无主瓣展宽）
- 非同步采样：基波在 bin 之间 → 平顶窗 + ±2 bin 取最大（容错）

代码通过 `use_window` 参数切换。

### 2. 高频粗扫频用更高采样率

50kHz @ 100kSPS 只有 2× 过采样，过零间隔少 → 测频不准。
高频段粗扫用 500kHz 采样率，仍可保证 < 1% 频率精度。

## 与 C 代码对应

| Python | C 文件 |
|---|---|
| `freq_detect_measure()` | `Algorithm/freq_detect.c::FreqDetect_Measure()` |
| `freq_detect_sync_rate()` | `Algorithm/freq_detect.c::FreqDetect_CalcSyncRate()` |
| `flat_top_window()` | `Algorithm/thd_measure.c::init_flat_top_window()` |
| `_fft()` | MSP432 用 CMSIS-DSP `arm_cfft_f32()` |
| `measure_thd()` | `Algorithm/thd_measure.c::measure_thd()` |

## 关键约束

- 修改 `config.h` 后须同步修改本脚本顶部常量
- MSP432 端必须用 CMSIS-DSP（FFT_SIZE=1024 软件浮点 ~ 50ms，硬件浮点 + DSP 库 ~ 1ms）
- ADC 必须做同步采样：`fs = freq × FFT_SIZE / M`
