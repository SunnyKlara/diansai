"""
algo_reference.py
=================

2026 模拟 多点温度采集系统 — 算法层 Python 金标准。

镜像范围：
  - <算法 1>（<C 文件 1>.c 等价实现）
  - <算法 2>（<C 文件 2>.c 等价实现）

主要价值：
  1. 验证算法理论精度（在合成信号上跑出 0 误差）
  2. MCU 移植后金标准对比（UART 导出原始数据 → 喂本脚本 → 对比）
"""

import math
import sys

# Windows GBK 终端兼容（防止 ✓/⚠ 等字符让 print 崩溃）
try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, ValueError):
    pass
import cmath
from dataclasses import dataclass, field
from typing import List, Tuple


# ============================================================
#  与 config.h 同步的常量
# ============================================================
SAMPLE_RATE = 10_000.0
DFT_N       = 1000


# ============================================================
#  核心数据结构
# ============================================================
@dataclass
class AlgoResult:
    value: float = 0.0


# ============================================================
#  与 C 1:1 等价的算法实现
# ============================================================
def algo_main(input_data: List[float]) -> AlgoResult:
    """TODO: 与 C 函数等价实现"""
    return AlgoResult(value=0.0)


# ============================================================
#  合成信号
# ============================================================
def synth_test_input() -> List[float]:
    """TODO: 合成已知答案的输入"""
    return [math.sin(2 * math.pi * 50 * k / SAMPLE_RATE) for k in range(DFT_N)]


# ============================================================
#  误差工具
# ============================================================
def err_pct(actual: float, expected: float) -> float:
    if abs(expected) < 1e-9:
        return 0.0
    return (actual - expected) / expected * 100.0


# ============================================================
#  验证主循环
# ============================================================
def run_validation() -> None:
    print("=" * 72)
    print(f" 2026 模拟 多点温度采集系统 — 算法层 PC 验证")
    print("=" * 72)
    print()

    # ---- 测试用例 1 ----
    inp = synth_test_input()
    result = algo_main(inp)
    print(f"  测试用例 1: result.value = {result.value:.4f}")
    print()

    print("=" * 72)
    print(" 结论：（待填）算法理论精度 < X%")
    print("=" * 72)


if __name__ == "__main__":
    run_validation()
