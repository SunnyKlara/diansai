# 跨题可复用代码片段

> 从 5 道真题中抽出的高频算法模板。**新题刷的时候，先来这里找现成的，再决定是否自己写**。

每个片段都有 **Python 参考实现 + C 等价实现**，且 **Python 已通过 PC 验证**（在 5 题里至少使用过一次）。

---

## 当前片段清单

| 片段 | Python | C | 用过的题目 | 用途 |
|---|---|---|---|---|
| `pid_controller` | ✅ | ✅ | 2024 H、2026 模拟 | 位置式 + 增量式 PID（含 anti-windup + ramp-up）|
| `dft_harmonic` | ✅ | ✅ | 2024 B、2021 A | 单频 DFT × N 谐波（无频谱泄漏）|
| `rms_meter` | ✅ | ✅ | 2024 B、2025 A | 滑动窗口 RMS |
| `sync_sample` | ✅ | ✅ | 2024 B、2021 A | 同步采样率计算 + 整周期采样工具 |
| `flat_top_window` | ✅ | ✅ | 2021 A | 平顶窗系数表 |
| `dual_loop_pi` | ✅ | — | 2026 模拟 | 双闭环 PI（电压外环 + 电流内环）|
| `dcdc_simple_model` | ✅ | — | 2026 模拟 | DC-DC 平均模型（Buck/Boost）|
| `calibration_fram` | — | ✅ | 2024 B | FRAM 持久化校准（含 CRC）|
| `uart_protocol` | — | ✅ | 2024 B | UART 命令分发（STAT/CAL/DUMP/RST）|

---

## 使用方式

```bash
# 复制到你的真题工程
cp 备赛系统/C_通用能力/算法层PC验证方法论/snippets/pid_controller.py \
   备赛系统/B_历年真题实战/2026/X_题名/01_代码/tests/pid_controller.py

cp 备赛系统/C_通用能力/算法层PC验证方法论/snippets/pid_controller.h \
   备赛系统/B_历年真题实战/2026/X_题名/01_代码/Algorithm/

cp 备赛系统/C_通用能力/算法层PC验证方法论/snippets/pid_controller.c \
   备赛系统/B_历年真题实战/2026/X_题名/01_代码/Algorithm/
```

或者在 algo_reference.py 顶部直接 import：

```python
import sys
from pathlib import Path
SNIPPETS = Path(__file__).resolve().parents[5] / \
    "C_通用能力" / "算法层PC验证方法论" / "snippets"
sys.path.insert(0, str(SNIPPETS))
from pid_controller import PIDController
```

---

## 设计原则

每个片段必须满足：

1. **Python 与 C 1:1 等价** —— 同样输入产出同样输出（差 < 1e-6）
2. **零外部依赖** —— Python 只用 stdlib，C 只用 math.h
3. **配置参数全部从外部传入** —— 不写死任何常量
4. **接口稳定** —— 已在某题用过，不轻易改
5. **每个片段都有最小用例** —— 在 `_example` 函数里能直接 `python xxx.py` 看到效果

---

## 维护

新增片段的标准流程：

1. 至少 2 题都用了同一个模式（避免过早抽象）
2. 在某题里抽出来 → 跑通该题验证 → 才放到这里
3. 更新本 README 表格 + 加一行"用过的题目"
