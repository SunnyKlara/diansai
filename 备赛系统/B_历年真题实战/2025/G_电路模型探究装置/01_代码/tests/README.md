# 2025 G 电路模型探究装置 — 算法层 PC 验证

`algo_reference.py` 是 **扫频测频响 + 滤波器分类 + FFT 推理**的 Python 等价实现。

```bash
python algo_reference.py
```

## 算法核心思想

```
学习阶段：       推理阶段：
DDS 扫频         输入信号
  ↓              ↓ FFT
被测电路         频域 × H(f)
  ↓ ADC 同步采样   ↓ IFFT
单频 DFT         输出（数字孪生）
  ↓
H(f) 查找表 ──────┘
```

**这道题的本质**：把模拟电路用数字 FFT "复刻"出来 → 系统辨识 + 数字孪生。

## 验证结果（已通过 ✅）

### 扫频精度（二阶 LP, fc=5kHz, Q=0.707）

| 指标 | 容差 | 实测 | 评价 |
|---|---|---|---|
| 增益误差 | 0.10 | 0.0349 | ✓ 余量 3× |
| 相位误差 | 0.17 rad | 0.0269 rad | ✓ 余量 6× |

### 滤波器类型识别

| 实际 | 识别 | 评价 |
|---|---|---|
| 二阶 LP (fc=5kHz) | LOWPASS | ✓ |
| 二阶 HP (fc=1kHz) | HIGHPASS | ✓ |
| 二阶 BP (fc=2kHz, Q=5) | BANDPASS | ✓ |

### FFT 推理验证

| 输入 | 理论 / 实测 | 评价 |
|---|---|---|
| 1kHz 单 sin 通过 LP | 增益 0.999 / 1.029 | ✓ 误差 3% |
| 1kHz + 10kHz 通过 LP | 峰值 1.26 在 [0.80, 1.34] 内 | ✓ |

## 与 C 代码的对应

| Python | C 文件 |
|---|---|
| `measure_one_frequency()` | `Algorithm/freq_response.c::FreqResp_Learn()` 单频内核 |
| `classify_filter()` | `Algorithm/freq_response.c::FreqResp_GetFilterType()` |
| `interp_freq_response()` | `Algorithm/freq_response.c::FreqResp_Interpolate()` |
| `infer_one_frame()` | `Algorithm/signal_processor.c::SigProc_ProcessFrame()` |
| `_fft / _ifft` | STM32 端用 CMSIS-DSP 的 `arm_rfft_fast_f32()` |

## 关键约束

- 修改 `config.h` 后须同步修改本脚本顶部常量
- 推理阶段 STM32F407 必须用 CMSIS-DSP（FFT_SIZE=2048 用纯 C 跑不动）
- ADC 双通道严格同步（TIM2 TRGO 同时触发），否则相位测量出错
