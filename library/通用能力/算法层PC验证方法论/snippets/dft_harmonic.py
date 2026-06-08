"""
dft_harmonic.py
===============

单频 DFT × N 谐波 + THD 计算 Python 参考实现。
（与 2024 B 的 dft_harmonic.c + 2021 A 的 thd_measure.c 等价）

核心思路：
  - 不做完整 FFT，只算需要的 N 个 bin（基波 + 谐波）
  - 共用基波 sin/cos LUT，谐波通过 (h × n) mod N 索引
  - 整周期采样下用矩形窗（无频谱泄漏，精度最高）

用法：
    from dft_harmonic import DFTHarmonic
    
    dft = DFTHarmonic(n_samples=1000, fund_bin=5, num_harmonics=10)
    result = dft.compute(samples)
    print(result.thd_percent, result.h_norm)
"""

import math
from dataclasses import dataclass, field
from typing import List


@dataclass
class HarmonicResult:
    magnitude: List[float] = field(default_factory=list)  # [0..N]
    rms: List[float]       = field(default_factory=list)
    h_norm: List[float]    = field(default_factory=list)
    thd_percent: float = 0.0


class DFTHarmonic:
    """整周期采样下的单频 DFT。
    
    Args:
        n_samples: DFT 长度（必须等于 整数周期 × 每周期点数）
        fund_bin:  基波在 DFT 中的 bin 索引（必须是整数）
        num_harmonics: 提取的谐波次数（含基波）
    """

    def __init__(self, n_samples: int, fund_bin: int, num_harmonics: int = 10):
        self.n = n_samples
        self.fund_bin = fund_bin
        self.num = num_harmonics
        # 预计算 sin/cos LUT
        self.sin_lut = [math.sin(2 * math.pi * k / n_samples) for k in range(n_samples)]
        self.cos_lut = [math.cos(2 * math.pi * k / n_samples) for k in range(n_samples)]

    def _bin_magnitude(self, x: List[float], bin_idx: int) -> float:
        re = im = 0.0
        for k in range(self.n):
            lut_idx = (bin_idx * k) % self.n
            re += x[k] * self.cos_lut[lut_idx]
            im -= x[k] * self.sin_lut[lut_idx]
        return math.sqrt(re * re + im * im) * 2.0 / self.n

    def compute(self, samples: List[float]) -> HarmonicResult:
        if len(samples) != self.n:
            raise ValueError(f"样本数 {len(samples)} != DFT 长度 {self.n}")

        result = HarmonicResult(
            magnitude=[0.0] * (self.num + 1),
            rms=[0.0] * (self.num + 1),
            h_norm=[0.0] * (self.num + 1),
        )

        # 提取各次谐波
        for h in range(1, self.num + 1):
            bin_idx = self.fund_bin * h
            if bin_idx >= self.n // 2:
                continue
            mag = self._bin_magnitude(samples, bin_idx)
            result.magnitude[h] = mag
            result.rms[h] = mag / math.sqrt(2.0)

        # 归一化 + THD
        result.h_norm[1] = 1.0
        if result.rms[1] > 1e-9:
            sum_sq = 0.0
            for h in range(2, self.num + 1):
                result.h_norm[h] = result.rms[h] / result.rms[1]
                sum_sq += result.rms[h] ** 2
            result.thd_percent = math.sqrt(sum_sq) / result.rms[1] * 100.0

        return result


def _example():
    """1kHz 基波 + H3=20% + H5=10% → 期望 THD = √(0.2² + 0.1²) × 100 = 22.36%"""
    fs = 20000.0    # 20 kHz（H5 = 5kHz，留充足奈奎斯特余量）
    freq = 1000.0
    # 1kHz @ 20kHz 采样，1000 点 → 50 个完整周期，fund_bin = 1000 × 1000 / 20000 = 50
    n_samples = 1000
    fund_bin = 50

    samples = [
        math.sin(2 * math.pi * freq * k / fs)
        + 0.2 * math.sin(2 * math.pi * 3 * freq * k / fs)
        + 0.1 * math.sin(2 * math.pi * 5 * freq * k / fs)
        for k in range(n_samples)
    ]

    dft = DFTHarmonic(n_samples=n_samples, fund_bin=fund_bin, num_harmonics=10)
    r = dft.compute(samples)

    expected_thd = math.sqrt(0.2 ** 2 + 0.1 ** 2) * 100
    print(f"基波幅值      : {r.magnitude[1]:.6f} (期望 1.0)")
    print(f"H3 / H1       : {r.h_norm[3]:.6f} (期望 0.2)")
    print(f"H5 / H1       : {r.h_norm[5]:.6f} (期望 0.1)")
    print(f"THD           : {r.thd_percent:.6f}% (期望 {expected_thd:.6f}%)")
    err = abs(r.thd_percent - expected_thd)
    print(f"误差          : {err:.6f}% {'✓' if err < 1e-4 else '✗'}")


if __name__ == "__main__":
    _example()
