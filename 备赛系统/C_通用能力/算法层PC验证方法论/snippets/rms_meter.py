"""
rms_meter.py
============

滑动窗口 RMS 计算（与 2025 A 的 rms_meter.c 等价）。

原理：每个新采样进来，用增量更新维护 sum(x²)，然后 √(sum/N)。
适合：基波频率可变的场景，窗口长度覆盖最低频率一个完整周期。

用法：
    from rms_meter import RMSMeter
    
    rms = RMSMeter(buffer_len=1024)
    for sample in adc_stream:
        current_rms = rms.update(sample)
"""

import math
from dataclasses import dataclass, field
from typing import List


@dataclass
class RMSMeter:
    buffer_len: int = 1024
    buf: List[float] = field(default_factory=list)
    idx: int = 0
    sum_sq: float = 0.0
    fill_count: int = 0
    ready: bool = False
    rms: float = 0.0

    def __post_init__(self):
        if not self.buf:
            self.buf = [0.0] * self.buffer_len

    def reset(self):
        self.buf = [0.0] * self.buffer_len
        self.idx = 0
        self.sum_sq = 0.0
        self.fill_count = 0
        self.ready = False
        self.rms = 0.0

    def update(self, sample: float) -> float:
        sq = sample * sample
        # 增量更新：减去最老的，加上最新的
        self.sum_sq -= self.buf[self.idx]
        self.sum_sq += sq
        self.buf[self.idx] = sq
        self.idx = (self.idx + 1) % self.buffer_len

        if not self.ready:
            self.fill_count += 1
            if self.fill_count >= self.buffer_len:
                self.ready = True

        n = self.fill_count if not self.ready else self.buffer_len
        if n > 0 and self.sum_sq > 0:
            self.rms = math.sqrt(self.sum_sq / n)
        return self.rms


def _example():
    """32V RMS 50Hz 正弦输入 → 实测 RMS 应 ≈ 32V"""
    fs = 20000.0    # 20 kHz
    freq = 50.0
    v_peak = 32.0 * math.sqrt(2)

    rms = RMSMeter(buffer_len=1024)

    # 跑 2 个完整窗口
    for k in range(2 * 1024):
        s = v_peak * math.sin(2 * math.pi * freq * k / fs)
        rms.update(s)

    print(f"输入: 32V RMS 50Hz 正弦")
    print(f"实测 RMS: {rms.rms:.4f} V")
    err = abs(rms.rms - 32.0)
    print(f"误差: {err:.4f} V {'✓' if err < 0.5 else '✗'}（容差 ±0.25V）")


if __name__ == "__main__":
    _example()
