"""
flat_top_window.py
==================

平顶窗系数（与 2021 A 的 init_flat_top_window 等价）。

何时用平顶窗？
  - 非整周期采样（频谱泄漏不可避免）
  - 关心幅度精度，不关心频率分辨率
  - 谐波 ±2 bin 内能找到主瓣的最大值

何时用矩形窗（不加窗）？
  - 整周期采样下，基波恰好在整数 bin
  - 此时矩形窗精度最高（无主瓣展宽）

抽自 2021 A 实战经验。
"""

import math
from typing import List


def flat_top_window(n: int) -> List[float]:
    """
    SR785 / Matlab 的 flattopwin 系数。
    主瓣宽 ~ 5 bin，副瓣低（< -90 dB）。
    幅度精度 < 0.01 dB。
    """
    # 标准 5 项系数
    a0, a1, a2, a3, a4 = (
        0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368
    )
    return [
        a0
        - a1 * math.cos(2 * math.pi * k / (n - 1))
        + a2 * math.cos(4 * math.pi * k / (n - 1))
        - a3 * math.cos(6 * math.pi * k / (n - 1))
        + a4 * math.cos(8 * math.pi * k / (n - 1))
        for k in range(n)
    ]


def hann_window(n: int) -> List[float]:
    """Hann 窗（一般用途，主瓣宽 ~ 4 bin）。"""
    return [0.5 - 0.5 * math.cos(2 * math.pi * k / (n - 1)) for k in range(n)]


def hamming_window(n: int) -> List[float]:
    """Hamming 窗（语音处理常用）。"""
    return [0.54 - 0.46 * math.cos(2 * math.pi * k / (n - 1)) for k in range(n)]


def apply_window(signal: List[float], window: List[float]) -> List[float]:
    """逐点相乘。返回加窗后的信号。"""
    return [signal[k] * window[k] for k in range(len(signal))]


def window_correction_factor(window: List[float]) -> float:
    """
    幅度修正因子：FFT 后单边谱要乘 2 / (sum_window) 才得到峰值。
    """
    return sum(window)


def _example():
    """对比矩形 / Hann / 平顶在非整周期采样下的精度。"""
    import math as m
    
    n = 1024
    fs = 1000.0
    # 非整周期：在 bin 5.3 处放一个幅度 1.0 的正弦
    freq = 5.3 * fs / n
    
    sig = [m.sin(2 * m.pi * freq * k / fs) for k in range(n)]
    
    for win_name, win_fn in [("rect", lambda nn: [1.0]*nn),
                              ("hann", hann_window),
                              ("flat-top", flat_top_window)]:
        win = win_fn(n)
        sig_w = apply_window(sig, win)
        
        # 估幅值：找 FFT 主峰
        re = im = 0.0
        re5 = im5 = 0.0; re6 = im6 = 0.0
        # 简化：只算 bin 5 和 bin 6
        for k in range(n):
            re5 += sig_w[k] * m.cos(2*m.pi*5*k/n); im5 -= sig_w[k] * m.sin(2*m.pi*5*k/n)
            re6 += sig_w[k] * m.cos(2*m.pi*6*k/n); im6 -= sig_w[k] * m.sin(2*m.pi*6*k/n)
        mag5 = m.sqrt(re5*re5 + im5*im5) * 2 / window_correction_factor(win)
        mag6 = m.sqrt(re6*re6 + im6*im6) * 2 / window_correction_factor(win)
        peak = max(mag5, mag6)
        print(f"  {win_name:8s}  bin5={mag5:.4f}  bin6={mag6:.4f}  peak={peak:.4f}  err={abs(peak-1.0):.4f}")


if __name__ == "__main__":
    print("非整周期信号（freq=5.3 bin），幅度精度对比：")
    _example()
    print()
    print("结论：平顶窗在非整周期下幅度误差最小")
    print("      但只在'不能做整周期采样'时才用，否则用矩形窗")
