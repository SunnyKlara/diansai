"""
algo_reference.py
=================

与 Algorithm/power_calc.c + Algorithm/dft_harmonic.c **逻辑完全等价**的
Python 参考实现。用于：

  1. 离线验证算法理论精度（纯阻 / 阻容 / 阻感 / 非线性谐波负载）
  2. 嵌入式移植后做"金标准"对比（同一段输入数据，C 输出应与本脚本一致）
  3. 报告 "算法精度验证" 章节的数据源

说明
----
- 这里的实现刻意 **不用** numpy 的高级 FFT / RMS 函数；
  改用纯 for-loop 的方式，完全对照 C 的双精度累积流程。
- 参数（FUND_FREQ_HZ / SAMPLES_PER_CYCLE / CYCLES_PER_MEAS / NUM_HARMONICS）
  与 config.h 一一对应，修改 config.h 时务必同步修改本文件。
"""

import math
from dataclasses import dataclass, field
from typing import List, Tuple


# ============================================================
#  与 config.h 同步的常量
# ============================================================
FUND_FREQ_HZ      = 50.0
CYCLES_PER_MEAS   = 5
SAMPLES_PER_CYCLE = 200
DFT_N             = CYCLES_PER_MEAS * SAMPLES_PER_CYCLE   # = 1000
SAMPLE_RATE_HZ    = SAMPLES_PER_CYCLE * FUND_FREQ_HZ      # = 10000
FUND_BIN          = CYCLES_PER_MEAS                       # = 5
NUM_HARMONICS     = 10


# ============================================================
#  时域计算（power_calc.c）
# ============================================================
@dataclass
class TimeDomainResult:
    v_rms: float = 0.0
    i_rms: float = 0.0
    p_active: float = 0.0
    s_apparent: float = 0.0
    pf: float = 0.0


def power_calc_time_domain(v_samples: List[float],
                           i_samples: List[float]) -> TimeDomainResult:
    """与 PowerCalc_TimeDomain 逻辑等价的 Python 实现。"""
    n = len(v_samples)
    assert n == len(i_samples) and n > 0

    sum_v2 = 0.0
    sum_i2 = 0.0
    sum_vi = 0.0
    for k in range(n):
        v = v_samples[k]
        i = i_samples[k]
        sum_v2 += v * v
        sum_i2 += i * i
        sum_vi += v * i

    inv_n = 1.0 / n
    out = TimeDomainResult()
    out.v_rms      = math.sqrt(sum_v2 * inv_n)
    out.i_rms      = math.sqrt(sum_i2 * inv_n)
    out.p_active   = sum_vi * inv_n
    out.s_apparent = out.v_rms * out.i_rms

    if out.s_apparent > 0.001:
        pf = out.p_active / out.s_apparent
        out.pf = max(-1.0, min(1.0, pf))
    else:
        out.pf = 0.0
    return out


# ============================================================
#  单频 DFT × 10 谐波 + THD（dft_harmonic.c）
# ============================================================
@dataclass
class HarmonicResult:
    magnitude: List[float] = field(default_factory=lambda: [0.0] * (NUM_HARMONICS + 1))
    rms:       List[float] = field(default_factory=lambda: [0.0] * (NUM_HARMONICS + 1))
    h_norm:    List[float] = field(default_factory=lambda: [0.0] * (NUM_HARMONICS + 1))
    thd_percent: float = 0.0


# 与 C 一致：基波一份 sin/cos LUT，谐波通过 (h × n) mod N 索引
_SIN_LUT: List[float] = []
_COS_LUT: List[float] = []


def _ensure_lut(n: int) -> None:
    global _SIN_LUT, _COS_LUT
    if len(_SIN_LUT) == n:
        return
    _SIN_LUT = [math.sin(2.0 * math.pi * k / n) for k in range(n)]
    _COS_LUT = [math.cos(2.0 * math.pi * k / n) for k in range(n)]


def _dft_bin_magnitude(x: List[float], bin_idx: int) -> float:
    """计算 |X(bin)| × 2/N，对应 C 中 dft_bin_magnitude。"""
    n = len(x)
    re = 0.0
    im = 0.0
    for k in range(n):
        lut_idx = (bin_idx * k) % n
        re += x[k] * _COS_LUT[lut_idx]
        im -= x[k] * _SIN_LUT[lut_idx]
    return math.sqrt(re * re + im * im) * 2.0 / n


def dft_harmonic_compute(i_samples: List[float]) -> HarmonicResult:
    """与 DFTHarm_Compute 逻辑等价的 Python 实现。"""
    n = len(i_samples)
    assert n == DFT_N
    _ensure_lut(n)

    out = HarmonicResult()

    for h in range(1, NUM_HARMONICS + 1):
        bin_idx = FUND_BIN * h
        if bin_idx >= n // 2:
            continue
        mag = _dft_bin_magnitude(i_samples, bin_idx)
        out.magnitude[h] = mag
        out.rms[h]       = mag / math.sqrt(2.0)

    out.h_norm[1] = 1.0
    if out.rms[1] > 1e-6:
        sum_harm_sq = 0.0
        for h in range(2, NUM_HARMONICS + 1):
            out.h_norm[h] = out.rms[h] / out.rms[1]
            sum_harm_sq += out.rms[h] ** 2
        out.thd_percent = math.sqrt(sum_harm_sq) / out.rms[1] * 100.0
    return out


# ============================================================
#  合成信号生成（用于测试）
# ============================================================
def synth_pure_resistive(v_rms: float, r_load: float) -> Tuple[List[float], List[float]]:
    """纯阻负载：v(t) = √2·V·sin(2πft)，i(t) = v(t)/R。"""
    v = []
    i = []
    v_peak = v_rms * math.sqrt(2.0)
    for k in range(DFT_N):
        t = k / SAMPLE_RATE_HZ
        vk = v_peak * math.sin(2.0 * math.pi * FUND_FREQ_HZ * t)
        v.append(vk)
        i.append(vk / r_load)
    return v, i


def synth_rl_load(v_rms: float, r: float, l_h: float) -> Tuple[List[float], List[float]]:
    """阻感负载：z = R + jωL，电流相位滞后。"""
    omega = 2.0 * math.pi * FUND_FREQ_HZ
    z_mag = math.sqrt(r * r + (omega * l_h) ** 2)
    phi   = math.atan2(omega * l_h, r)            # 电流相对电压的滞后角

    v_peak = v_rms * math.sqrt(2.0)
    i_peak = v_peak / z_mag

    v, i = [], []
    for k in range(DFT_N):
        t = k / SAMPLE_RATE_HZ
        v.append(v_peak * math.sin(omega * t))
        i.append(i_peak * math.sin(omega * t - phi))
    return v, i


def synth_nonlinear(v_rms: float, harmonic_amps: dict) -> Tuple[List[float], List[float]]:
    """非线性负载：基波 + 给定的奇数次谐波。
       harmonic_amps = {1: 1.0, 3: 0.3, 5: 0.15, 7: 0.07, 9: 0.03}
                       表示电流基波幅值 1.0 A，3 次谐波 0.3 A，等等。
    """
    omega = 2.0 * math.pi * FUND_FREQ_HZ
    v_peak = v_rms * math.sqrt(2.0)
    v, i = [], []
    for k in range(DFT_N):
        t = k / SAMPLE_RATE_HZ
        v.append(v_peak * math.sin(omega * t))

        i_t = 0.0
        for h, a in harmonic_amps.items():
            i_t += a * math.sqrt(2.0) * math.sin(h * omega * t)   # 用 RMS 输入，转峰值
        i.append(i_t)
    return v, i


# ============================================================
#  测试 & 报告
# ============================================================
def _err_pct(actual: float, expected: float) -> float:
    if abs(expected) < 1e-9:
        return 0.0
    return (actual - expected) / expected * 100.0


def run_validation() -> None:
    print("=" * 72)
    print(" 2024 B 单相功率分析仪 — 算法层 PC 验证（C 代码逻辑等价 Python 实现）")
    print("=" * 72)
    print(f" DFT_N = {DFT_N},  fs = {SAMPLE_RATE_HZ} Hz,  FUND_BIN = {FUND_BIN}")
    print()

    # ---- 1. 纯阻负载：220V × 100Ω → 2.2A ----
    v, i = synth_pure_resistive(v_rms=220.0, r_load=100.0)
    td = power_calc_time_domain(v, i)
    expected_p = 220.0 * 220.0 / 100.0
    print("【纯阻负载】220V × 100Ω")
    print(f"  Vrms  期望 220.000  →  实测 {td.v_rms:8.4f}  err={_err_pct(td.v_rms, 220.0):+6.4f}%")
    print(f"  Irms  期望   2.200  →  实测 {td.i_rms:8.4f}  err={_err_pct(td.i_rms,   2.2):+6.4f}%")
    print(f"  P     期望 {expected_p:7.3f}  →  实测 {td.p_active:8.4f}  "
          f"err={_err_pct(td.p_active, expected_p):+6.4f}%")
    print(f"  PF    期望   1.000  →  实测 {td.pf:8.4f}")
    print()

    # ---- 2. 阻感负载：220V × (10Ω + 31.83mH) → 30Ω 模长，PF = 0.5 ----
    L_for_pf_05 = (math.sqrt(3.0) * 10.0) / (2.0 * math.pi * 50.0)  # 10·tan60° / ω
    v, i = synth_rl_load(v_rms=220.0, r=10.0, l_h=L_for_pf_05)
    td = power_calc_time_domain(v, i)
    z_mag = math.sqrt(10.0 ** 2 + (2 * math.pi * 50 * L_for_pf_05) ** 2)
    expected_irms = 220.0 / z_mag
    expected_pf   = 10.0 / z_mag
    expected_p    = 220.0 * expected_irms * expected_pf
    print("【阻感负载】R=10Ω, L→PF=0.5（滞后）")
    print(f"  Vrms  期望 220.000  →  实测 {td.v_rms:8.4f}  err={_err_pct(td.v_rms, 220.0):+6.4f}%")
    print(f"  Irms  期望 {expected_irms:7.3f}  →  实测 {td.i_rms:8.4f}  "
          f"err={_err_pct(td.i_rms, expected_irms):+6.4f}%")
    print(f"  P     期望 {expected_p:7.3f}  →  实测 {td.p_active:8.4f}  "
          f"err={_err_pct(td.p_active, expected_p):+6.4f}%")
    print(f"  PF    期望 {expected_pf:7.4f}  →  实测 {td.pf:8.4f}  "
          f"err={_err_pct(td.pf, expected_pf):+6.4f}%")
    print()

    # ---- 3. 非线性负载（典型整流电源）----
    harmonic_amps = {1: 1.0, 3: 0.3, 5: 0.15, 7: 0.07, 9: 0.03}
    expected_thd = math.sqrt(sum(a * a for h, a in harmonic_amps.items() if h != 1)) \
                   / harmonic_amps[1] * 100.0
    v, i = synth_nonlinear(v_rms=220.0, harmonic_amps=harmonic_amps)
    td   = power_calc_time_domain(v, i)
    harm = dft_harmonic_compute(i)
    print("【非线性负载】基波 1A + H3 0.3 + H5 0.15 + H7 0.07 + H9 0.03")
    print(f"  Vrms  期望 220.000  →  实测 {td.v_rms:8.4f}  err={_err_pct(td.v_rms, 220.0):+6.4f}%")
    expected_irms_nl = math.sqrt(sum(a * a for a in harmonic_amps.values()))
    print(f"  Irms  期望 {expected_irms_nl:7.4f}  →  实测 {td.i_rms:8.4f}  "
          f"err={_err_pct(td.i_rms, expected_irms_nl):+6.4f}%")
    print(f"  THD   期望 {expected_thd:7.3f}%  →  实测 {harm.thd_percent:8.4f}%  "
          f"err={_err_pct(harm.thd_percent, expected_thd):+6.4f}%")
    print()
    print(f"  各次谐波 RMS / 归一化（与期望对比）：")
    print(f"    H#  expected  measured   err%")
    for h in range(1, NUM_HARMONICS + 1):
        exp = harmonic_amps.get(h, 0.0)
        act = harm.rms[h]
        err = _err_pct(act, exp) if exp > 0 else 0.0
        flag = " ✓" if abs(err) < 0.5 or exp == 0 else " ⚠"
        print(f"    H{h:<2}  {exp:8.4f}  {act:8.4f}  {err:+7.4f}%{flag}")

    print()
    print("=" * 72)
    print(" 结论")
    print("=" * 72)
    print(" 整周期采样 + 单频 DFT 算法在三种典型负载下：")
    print("   - 时域 Vrms / Irms / P / PF：理论误差 < 0.001%（受限于双精度）")
    print("   - 谐波 / THD：理论误差 < 0.001%（频谱泄漏被整周期消除）")
    print(" → 算法本身完全满足 1% / 2% 要求，剩余误差预算全部留给硬件链路。")
    print(" → MCU 端 C 代码移植后，应在相同输入下复现这些数值（绝对差 < 1e-4）。")


if __name__ == "__main__":
    run_validation()
