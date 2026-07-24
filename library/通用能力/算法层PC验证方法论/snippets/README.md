# 跨题可复用代码片段

> 从历年真题**审题分析**中抽出的高频算法模板。新题先来这里找现成的，再决定是否自己写。
> 每个片段是 **Python 参考实现**，自带 `_example` 最小自测（`python xxx.py` 可直接看效果）；**未经真机**，属 PC 可验证层。

---

## 当前片段清单

| 片段 | Python | 适用场景 | 用途 |
|---|---|---|---|
| `pid_controller` | ✅ | 控制 / 小车 / 悬浮 | 位置式 + 增量式 PID（含 anti-windup + ramp-up）|
| `dft_harmonic` | ✅ | 仪表 / 谐波分析 | 单频 DFT × N 谐波（无频谱泄漏）|
| `rms_meter` | ✅ | 仪表 / 功率 | 滑动窗口 RMS |
| `sync_sample` | ✅ | 仪表 / 频谱 | 同步采样率计算 + 整周期采样 |
| `flat_top_window` | ✅ | 频谱幅值测量 | 平顶窗系数表 |
| `dual_loop_pi` | ✅ | 电源 / 变流器 | 双闭环 PI（电压外环 + 电流内环）|
| `dcdc_simple_model` | ✅ | 电源仿真 | DC-DC 平均模型（Buck / Boost）|

> C 版本未包含；需要时按 Python 版手动移植到工程 `Algorithm/` 层（接口保持一致）。

---

## 使用方式

复制 Python 参考进题目工程做 PC 验证：

```bash
cp library/通用能力/算法层PC验证方法论/snippets/pid_controller.py \
   workbench/2026-X_题名/工程_平台名/tests/
```

或在 `algo_reference.py` 顶部 import（向上找仓库根，稳）：

```python
import sys
from pathlib import Path
p = Path(__file__).resolve()
while not (p / "library").exists() and p != p.parent:
    p = p.parent
sys.path.insert(0, str(p / "library" / "通用能力" / "算法层PC验证方法论" / "snippets"))
from pid_controller import PIDController
```

---

## 设计原则

每个片段满足：

1. **零外部依赖** —— 只用 Python stdlib。
2. **配置全部外部传入** —— 不写死常量。
3. **自带最小用例** —— `_example` 里能直接 `python xxx.py` 看到效果。
4. **接口稳定** —— 抽出后不轻易改。

## 维护

新增片段的标准流程：

1. 至少 2 处场景都用到同一模式（避免过早抽象）。
2. 写好 `_example` 自测跑通 → 才放进来。
3. 更新本 README 表格。
