"""
sync_sample.py
==============

同步采样工具：保证 DFT_N 个点恰好覆盖 M 个完整基波周期，消除频谱泄漏。

公式：
    sync_fs = freq × N / M
    fund_bin = M  （在 DFT 中精确为整数）

抽自 2024 B (整周期 1000 点) + 2021 A (FFT_SIZE=1024 整周期搜索)。

用法：
    from sync_sample import calc_sync_rate, calc_fund_bin
    
    sync_fs, m = calc_sync_rate(freq=50.0, n_samples=1000, fs_target=10000.0)
    bin = calc_fund_bin(m)   # = m
"""

import math
from typing import Tuple


def calc_sync_rate(freq: float, n_samples: int, fs_target: float) -> Tuple[float, int]:
    """
    选 M 个完整周期，使 sync_fs 最接近 fs_target。

    Args:
        freq:       信号基波频率（Hz）
        n_samples:  DFT 长度
        fs_target:  期望采样率（Hz）

    Returns:
        (sync_fs, m_cycles)
    """
    if freq <= 0:
        return (fs_target, 1)
    m_ideal = freq * n_samples / fs_target
    m = max(1, round(m_ideal))
    sync_fs = freq * n_samples / m
    return (sync_fs, m)


def calc_fund_bin(m_cycles: int) -> int:
    """整周期采样下，基波 bin = M（周期数）。"""
    return m_cycles


def best_n_for_freq(freq: float, fs_target: float,
                    n_min: int = 100, n_max: int = 4096) -> Tuple[int, float, int]:
    """
    给定频率和目标采样率，找最佳的 (N, fs, M) 让 fs 最接近目标。

    用于"采样点数可调"的场景。返回的 N 不要求是 2 的幂。
    """
    best = None
    for n in range(n_min, n_max + 1):
        m = max(1, round(freq * n / fs_target))
        fs = freq * n / m
        diff = abs(fs - fs_target) / fs_target
        if best is None or diff < best[3]:
            best = (n, fs, m, diff)
    return best[:3]


def _example():
    """50Hz 工频，目标 10kHz 采样率，DFT_N = 1000"""
    freq = 50.0
    n = 1000
    fs_target = 10000.0

    sync_fs, m = calc_sync_rate(freq, n, fs_target)
    fund_bin = calc_fund_bin(m)

    print(f"freq          = {freq} Hz")
    print(f"n_samples     = {n}")
    print(f"fs_target     = {fs_target} Hz")
    print(f"---")
    print(f"sync_fs       = {sync_fs} Hz")
    print(f"m_cycles      = {m}")
    print(f"fund_bin      = {fund_bin} (整数 ✓ 无频谱泄漏)")
    print(f"误差          = {abs(sync_fs - fs_target) / fs_target * 100:.4f}%")

    # 1kHz 信号，1024 点
    print()
    print("---- 1kHz 信号，1024 点（找最佳 N）----")
    n_opt, fs_opt, m_opt = best_n_for_freq(1000.0, 100000.0, 100, 1024)
    print(f"最佳: N={n_opt}, fs={fs_opt:.2f}, M={m_opt}")


if __name__ == "__main__":
    _example()
