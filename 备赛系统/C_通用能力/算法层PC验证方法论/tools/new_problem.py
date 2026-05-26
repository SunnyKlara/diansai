"""
new_problem.py
==============

新题脚手架生成器：拿到新题时一键生成符合电赛备赛仓库规范的目录结构。

生成的内容：
  - 标准目录（00/01/02/03/04 + tests）
  - 占位 markdown（00_做题指南、00_深度审题、设计报告、调试清单等）
  - 算法层 / 驱动层 / Core 占位 C 文件
  - tests/algo_reference.py 骨架
  - tests/cal_helper.py 骨架
  - tests/README.md 骨架

用法：
  python new_problem.py 2026 X 题名
  python new_problem.py 2026 X 题名 --mcu STM32G474 --topic power
  python new_problem.py --list-topics

支持的 topic（决定算法层生成哪些占位文件）：
  power     —— 电源 / 变流器（SVPWM、双闭环、保护）
  measure   —— 仪器仪表（DFT、RMS、频响、校准）
  control   —— 控制 / 小车 / 飞行器（PID、滤波、状态机）
  signal    —— 信号处理（FFT、滤波、谐波）
  generic   —— 通用（默认）
"""

import argparse
import sys
from pathlib import Path
from datetime import datetime


def find_repo_root() -> Path:
    p = Path(__file__).resolve()
    for _ in range(10):
        p = p.parent
        if (p / ".git").exists() or (p / ".kiro").exists():
            return p
    raise RuntimeError("找不到仓库根（缺少 .git 或 .kiro 标记）")


# ============================================================
#  各文件模板
# ============================================================
def tmpl_problem_guide(year: str, code: str, name: str, mcu: str) -> str:
    return f"""# {year} {code} {name} —— 做题指南

> 配套：`00_深度审题与方案论证.md`（5 视角分析）

## 评分策略（待补：根据题目分项填）

| 项目 | 分值 | 难度 | 策略 |
|------|------|------|------|
| (1) | XX | ★★ | 必拿 |
| (2) | XX | ★★★ | 重点 |
| 报告 | 20 | ★★ | 必拿 18+ |

## MCU 选型

**推荐**：{mcu}（理由：见 00_深度审题与方案论证.md）

## 4 天分配

```
Day 1: 审题 + 焊接 + 驱动调通
Day 2: 算法实现 + 闭环
Day 3: 联调 + 报告主体
Day 4: 报告打磨 + 提交
```

## 关键路径（按顺序）

1. 写完 `00_深度审题与方案论证.md`（不能跳过）
2. 算法层接口在头文件中定义清楚
3. **写 `01_代码/tests/algo_reference.py` 验证算法 0 误差**（关键节点）
4. Drivers 占位 + Core 状态机
5. 烧录联调 → cal_helper 校准 → 实测精度

## 反模式

❌ 跳过审题直接写代码
❌ 没跑通算法层 PC 验证就上 MCU
❌ 不写校准流程
❌ 报告留到最后 2 小时

## 当前进度（Markdown checkbox）

- [ ] 深度审题完成
- [ ] 做题指南完成
- [ ] BOM 完成
- [ ] 电路设计说明完成
- [ ] 算法层 PC 验证通过
- [ ] 代码框架完成
- [ ] 调试清单完成
- [ ] 报告大纲完成
"""


def tmpl_deep_analysis(year: str, code: str, name: str) -> str:
    return f"""# {year} {code} {name} —— 深度审题与方案论证

> **5 视角分析**：评委、工程师、报告评审、安全审核员、架构师。

---

## 第一层：题目的精确解读

### 题目要求的精确数字

| 项 | 要求 |
|---|---|
| ... | ... |

### 隐含约束（题目没明说但必须遵守）

**约束 1**：（待填）

**约束 2**：（待填）

### 评委会怎么测试？

（待填：模拟评测流程）

---

## 第二层：误差预算（工程师视角）

### 误差链

```
... → ... → ... → ...
```

### 逐环节误差分析

| 误差源 | 量级 | 处理 |
|---|---|---|
| ... | ... | ... |

### 总误差 RSS

（待填：算出总误差 vs 题目容差 → 预测达标余量）

---

## 第三层：MCU 与器件选型论证

### MCU 候选对比

| MCU | 主频 | ADC | 价格 | 适合度 |
|---|---|---|---|---|
| ... | ... | ... | ... | ... |

### 关键器件对比

（运放 / 显示 / 传感器 / 电源等）

---

## 第四层：软件架构

### 三层结构

```
Core/main.c          状态机 + LPM
  ├── Drivers/       硬件抽象
  │   ├── ...
  │   └── ...
  └── Algorithm/     与硬件解耦
      ├── ...
      └── ...
```

### 关键算法

（待填：算法名 + 计算量 + MCU 端预估时间）

---

## 第五层：硬件难点

### 难点 1：（待填）
**对策**：

### 难点 2：（待填）
**对策**：

---

## 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| ... | ... | ... | ... |

## 评分预估

| 项 | 满分 | 预估 |
|---|---|---|
| (1) | XX | XX |
| (2) | XX | XX |

合计：__/120
"""


def tmpl_bom() -> str:
    return """# 材料清单 BOM

## 主控

| 序号 | 器件 | 型号 | 数量 | 单价 | 用途 | 备注 |
|---|---|---|---|---|---|---|
| 1 | MCU | xxx | 1 | ¥XX | 主控 | |

## 传感器

| 序号 | 器件 | 型号 | 数量 | 单价 | 关键参数 | 备注 |
|---|---|---|---|---|---|---|
| 1 | ... | ... | 1 | ¥XX | ... | |

## 信号调理

| 序号 | 器件 | 型号 | 数量 | 单价 | 用途 |
|---|---|---|---|---|---|
| 1 | 运放 | OPA2333 | 2 | ¥10 | 低功耗调理 |

## 电源

| 序号 | 器件 | 型号 | 数量 | 单价 | 用途 |
|---|---|---|---|---|---|
| 1 | LDO | TPS7A02 | 1 | ¥5 | 主电源 |

## 总预算

| 类别 | 金额 |
|---|---|
| 主控 | ¥XX |
| 传感器 | ¥XX |
| 调理 | ¥XX |
| 电源 | ¥XX |
| 通用 | ¥20 |
| **合计** | **¥XX** |
"""


def tmpl_circuit_doc() -> str:
    return """# 电路设计说明

## 一、系统信号链路

```
[输入] → [前级] → [调理] → [ADC] → [MCU] → [DAC/PWM] → [后级] → [输出]
```

## 二、关键电路

### 信号采样

（待填：互感器 / 分压 / 偏置 / 缓冲）

### 信号输出

（待填：DAC / PWM / 驱动）

### 抗混叠滤波器

（待填：fc 选择依据）

## 三、低功耗设计要点

| 点 | 措施 |
|---|---|
| LDO | TPS7A02（25nA 静态）|
| 上拉电阻 | 100kΩ |
| 显示 | 段码 LCD |

## 四、安全规则（如涉及强电）

⚠️ （待填）

## 五、PCB 布局要点

1. 强弱电隔离 ≥ 5mm
2. 模拟 / 数字地分割
3. ADC 输入线靠 GND

## 六、测试点预留

```
TP1: ...
TP2: ...
```
"""


def tmpl_design_report(year: str, code: str, name: str) -> str:
    return f"""# {year} 全国大学生电子设计竞赛 {code} 题
# {name} 设计报告

**摘要**：（提交前最后写）

**关键词**：（3~5 个）

---

## 1. 系统方案论证

### 1.1 任务分析

（题目要求 + 核心矛盾）

### 1.2 方案对比与选择

#### 关键模块对比

| 方案 | 优点 | 缺点 | 评分 |
|---|---|---|---|
| ... | ... | ... | ... |

### 1.3 系统总体框图

详见 `02_硬件/电路设计说明.md`。

## 2. 理论分析与计算

### 2.1 关键公式

$$ ... $$

### 2.2 误差预算

| 误差源 | 量级 |
|---|---|
| ... | ... |
| **总误差 RSS** | **< X%** ✓ |

### 2.3 功耗 / 性能预算（如适用）

## 3. 电路与程序设计

### 3.1 主电路设计

详见 `02_硬件/电路设计说明.md`。

### 3.2 程序架构

详见 `01_代码/README.md`。

### 3.3 关键算法

详见 `01_代码/tests/algo_reference.py`，已通过 PC 仿真验证。

## 4. 测试方案与测试结果

### 4.1 测试设备

- ...

### 4.2 测试结果

> 实测填 `03_报告/测试数据模板.md` 后回贴。

| 项 | 要求 | 实测 | 误差 | 满足 |
|---|---|---|---|---|
| ... | ... | ... | ... | ☐ |

## 5. 总结

**创新点**：
1. 算法层 PC 金标准验证（理论精度 < X%，远低于容差 Y%）
2. ...

**不足**：
1. ...

---

## 参考文献

[1] ...

## 附录

- A. BOM：`02_硬件/材料清单_BOM.md`
- B. 电路设计：`02_硬件/电路设计说明.md`
- C. 程序源代码：`01_代码/`
- D. 算法验证：`01_代码/tests/`
- E. 测试原始数据：`03_报告/测试数据模板.md`
"""


def tmpl_test_template() -> str:
    return """# 测试数据记录模板

## 表 1 基本要求测试

| 项 | 要求 | 实测 1 | 实测 2 | 实测 3 | 平均 | 误差 | 满足 |
|---|---|---|---|---|---|---|---|
| ... | ... | ___ | ___ | ___ | ___ | ___% | ☐ |

## 表 2 发挥部分测试

| 项 | 要求 | 实测 | 误差 | 满足 |
|---|---|---|---|---|
| ... | ... | ___ | ___% | ☐ |

## 测试设备

- 标准仪器：（型号 + 精度）
- 示波器：
- 万用表：
"""


def tmpl_debug_checklist() -> str:
    return """# 调试检查清单

## Day 1 焊接

### 安全检查
- [ ] ...

### 采样电路
- [ ] ...

### 显示
- [ ] ...

## Day 2 软件调试

### 算法层金标准（PC 验证）
- [ ] `python tests/algo_reference.py` 全过 ✓
- [ ] 关键指标余量倍数 ≥ 3×

### MCU 移植对比
- [ ] UART STAT 命令工作
- [ ] UART DUMP 导出原始数据
- [ ] PC 端 cal_helper.py --verify 与 algo_reference 差值 < 1e-4

### 校准
- [ ] 接标准基准 → CAL 命令 → 写入 FRAM
- [ ] 断电再上电 → BOOT cal_loaded=1
- [ ] 校准后实测精度达标

## Day 3 全任务联测

- [ ] 多场景测试（典型 + 边界）
- [ ] 报告大纲完成

## Day 4 最终

- [ ] 误差全部达标
- [ ] 报告打印
- [ ] 备份代码 + 提交准备

## 常见故障

| 问题 | 原因 | 解决 |
|---|---|---|
| ... | ... | ... |
"""


def tmpl_lessons_learned(year: str, code: str, name: str) -> str:
    return f"""# {year} {code} {name} —— 独到经验总结

> 调试过程中遇到的坑 + 解决思路。每个 bug 写一段。

---

## 一、赛前必知的 N 个坑

### 坑 1：（待填）

**症状**：
**根因**：
**对策**：

### 坑 2：（待填）

---

## 二、调试期记录

### 模板

```
### YYYY-MM-DD HH:MM  [bug 标题]

**现象**：
**预期**：
**实际**：
**根因**：
**解决**：
**下次预防**：
**关联文件**：
```

---

## 三、算法层 PC 验证金标准（参考）

参见 `01_代码/tests/algo_reference.py` 输出值。MCU 移植后实测应与此一致（差 < 1e-4）。
"""


def tmpl_code_readme(year: str, code: str, name: str, mcu: str) -> str:
    return f"""# {year} {code} {name} — 代码工程说明

## 工程结构

```
01_代码/
├── README.md
├── config.h
├── Core/main.c
├── Drivers/        # 硬件抽象（含 calibration + uart_dbg）
├── Algorithm/      # 与硬件解耦，可单元测试
└── tests/          # PC 端验证 / 调试工具
    ├── algo_reference.py
    ├── cal_helper.py
    └── README.md
```

## MCU

{mcu}

## 关键算法

（待填）

## UART 调试协议

参见 `备赛系统/C_通用能力/算法层PC验证方法论/02_UART调试协议规范.md`。

```
> STAT       # 看实时数据
> CAL ...    # 校准
> DUMP       # 导出原始 ADC（用于金标准对比）
> RST        # 出厂复位
```

## 算法验证

```bash
python tests/algo_reference.py
```

应输出 "ALL PASSED"。如果某项失败，先排查 config.h 是否与脚本顶部常量一致。

## 标杆参考

- `备赛系统/B_历年真题实战/2024/B_单相功率分析仪/`（仪表 / 校准链路完整）
- `备赛系统/B_历年真题实战/2024/H_自动行驶小车/`（控制 / PID）
- `备赛系统/C_通用能力/算法层PC验证方法论/`（方法论）
"""


def tmpl_config_h(name: str) -> str:
    return f"""/**
 * @file    config.h
 * @brief   {name} —— 全局可调参数
 * @note    所有调试用的参数集中在这里。修改 config.h 时，
 *          tests/algo_reference.py 顶部常量必须同步修改。
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>

/* ============================================================
 *  1. 系统参数
 * ============================================================ */
// （待填）

/* ============================================================
 *  2. ADC / 采样
 * ============================================================ */
// （待填）

/* ============================================================
 *  3. 算法参数（PID / 阈值 / 容差）
 * ============================================================ */
// （待填）

/* ============================================================
 *  4. 校准
 * ============================================================ */
#define CALIB_MAGIC      0xCA11A6E0UL   /* FRAM 校准数据有效标记 */

#endif /* __CONFIG_H */
"""


def tmpl_main_c(name: str) -> str:
    return f"""/**
 * @file    main.c
 * @brief   {name} 主程序
 *
 *  状态机：
 *      ST_BOOT      上电初始化
 *      ST_MEASURE   测量
 *      ST_PROCESS   计算
 *      ST_DISPLAY   显示
 *      ST_LPM       低功耗
 */

#include <stdint.h>
#include "../config.h"

/* TODO_PORT: include hardware-specific headers here */

int main(void) {{
    /* TODO_PORT: clock + interrupts + LPM init */
    
    while (1) {{
        /* run_measure_cycle(); */
        /* UART_PollCommands(); */
    }}
}}
"""


def tmpl_algo_reference(year: str, code: str, name: str) -> str:
    return f'''"""
algo_reference.py
=================

{year} {code} {name} — 算法层 Python 金标准。

镜像范围：
  - <算法 1>（<C 文件 1>.c 等价实现）
  - <算法 2>（<C 文件 2>.c 等价实现）

主要价值：
  1. 验证算法理论精度（在合成信号上跑出 0 误差）
  2. MCU 移植后金标准对比（UART 导出原始数据 → 喂本脚本 → 对比）
"""

import math
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
    print(f" {year} {code} {name} — 算法层 PC 验证")
    print("=" * 72)
    print()

    # ---- 测试用例 1 ----
    inp = synth_test_input()
    result = algo_main(inp)
    print(f"  测试用例 1: result.value = {{result.value:.4f}}")
    print()

    print("=" * 72)
    print(" 结论：（待填）算法理论精度 < X%")
    print("=" * 72)


if __name__ == "__main__":
    run_validation()
'''


def tmpl_cal_helper(year: str, code: str, name: str) -> str:
    return f'''"""
cal_helper.py
=============

PC 端校准 / 调试助手（与装置通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat        # 看实时数据
  python cal_helper.py --port COM3 --calibrate   # 交互式校准
  python cal_helper.py --port COM3 --dump out.csv # 导出原始数据
  python cal_helper.py --port COM3 --verify out.csv # 跑金标准
  python cal_helper.py --port COM3 --reset       # 出厂复位
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import algo_reference as alg


def open_serial(port: str, baud: int = 115200):
    try:
        import serial  # type: ignore
    except ImportError:
        print("ERR: 缺少 pyserial，请先 `pip install pyserial`", file=sys.stderr)
        sys.exit(2)
    return serial.Serial(port, baudrate=baud, timeout=2)


def cmd(ser, line: str, wait_s: float = 0.5) -> str:
    ser.reset_input_buffer()
    ser.write((line + "\\r\\n").encode())
    ser.flush()
    time.sleep(wait_s)
    return ser.read_all().decode(errors="replace")


def do_stat(ser):
    print(cmd(ser, "STAT"))


def do_calibrate(ser):
    print("[1] 当前实测：")
    print("    " + cmd(ser, "STAT").strip())
    real_val = input("[2] 输入标准基准值（按格式 'V=220.0 I=2.20'）: ").strip()
    print("[3] 发送校准命令")
    print("    " + cmd(ser, f"CAL {{real_val}}").strip())


def do_dump(ser, out_csv: str):
    """TODO: 根据题目调整 header 和数据格式"""
    ser.reset_input_buffer()
    ser.write(b"DUMP\\r\\n")
    ser.flush()
    lines = []
    started = False
    deadline = time.time() + 30.0
    while time.time() < deadline:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        if line == "DUMP_START":
            started = True; continue
        if line == "DUMP_END":
            break
        if started:
            lines.append(line)
    
    Path(out_csv).write_text("raw\\n" + "\\n".join(lines) + "\\n", encoding="utf-8")
    print(f"OK 写入 {{out_csv}}（{{len(lines)}} 行）")


def do_verify(csv_path: str):
    """TODO: 用 algo_reference 跑一遍并输出对比"""
    print(f"读 {{csv_path}}（待实现）")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="{year} {code} {name} PC 校准助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true")
    g.add_argument("--calibrate", action="store_true")
    g.add_argument("--dump", metavar="CSV")
    g.add_argument("--verify", metavar="CSV")
    g.add_argument("--reset", action="store_true")
    args = ap.parse_args()

    if args.verify:
        do_verify(args.verify); return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)
    
    ser = open_serial(args.port, args.baud)
    try:
        if args.stat: do_stat(ser)
        elif args.calibrate: do_calibrate(ser)
        elif args.dump: do_dump(ser, args.dump)
        elif args.reset: do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
'''


def tmpl_tests_readme(year: str, code: str, name: str) -> str:
    return f"""# {year} {code} {name} — 算法层 PC 验证

`algo_reference.py` 是算法层的 Python 等价实现，PC 直接跑：

```bash
python algo_reference.py
```

## 用途

1. 验证算法理论精度
2. MCU 移植后金标准对比
3. 给报告"算法验证"章节提供数据

## 验证矩阵（待填）

| 测试场景 | 期望 | 实测 | 误差 | 评价 |
|---|---|---|---|---|
| ... | ... | ... | ... | ✓/⚠ |

## 与 C 代码对应

| Python | C 文件 |
|---|---|
| `algo_main()` | `Algorithm/xxx.c::XXX_Compute()` |

## 关键约束

- 修改 `config.h` 后须同步修改本脚本顶部常量
"""


# ============================================================
#  topic 决定生成哪些算法占位文件
# ============================================================
TOPIC_ALGO_FILES = {
    "power":   ["svpwm_3phase", "rms_meter", "voltage_loop"],
    "measure": ["dft_engine", "rms_meter", "calibration"],
    "control": ["pid", "filter", "state_machine"],
    "signal":  ["fft_engine", "thd_calc", "freq_detect"],
    "generic": ["algo_main"],
}


# ============================================================
#  主流程
# ============================================================
def write_if_not_exists(path: Path, content: str):
    if path.exists():
        print(f"  - skip (existed) {path.name}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"  + create {path.name}")


def generate(year: str, code: str, name: str, mcu: str, topic: str, repo_root: Path):
    base = repo_root / "备赛系统" / "B_历年真题实战" / year / f"{code}_{name}"
    if base.exists() and any(base.iterdir()):
        print(f"⚠️  目录 {base.relative_to(repo_root)} 已存在且非空")
        ans = input("    继续生成（缺什么补什么，不覆盖已有文件）? [y/N]: ").strip().lower()
        if ans != "y":
            print("已取消")
            return

    print(f"\n📁 生成目录: {base.relative_to(repo_root)}")
    print(f"   MCU: {mcu}")
    print(f"   Topic: {topic}\n")

    print("[根级 markdown]")
    write_if_not_exists(base / "00_做题指南.md", tmpl_problem_guide(year, code, name, mcu))
    write_if_not_exists(base / "00_深度审题与方案论证.md", tmpl_deep_analysis(year, code, name))

    print("\n[01_代码]")
    write_if_not_exists(base / "01_代码" / "README.md", tmpl_code_readme(year, code, name, mcu))
    write_if_not_exists(base / "01_代码" / "config.h", tmpl_config_h(name))
    write_if_not_exists(base / "01_代码" / "Core" / "main.c", tmpl_main_c(name))
    
    # 占位 .gitkeep
    for sub in ("Core", "Drivers", "Algorithm"):
        gk = base / "01_代码" / sub / ".gitkeep"
        if not (base / "01_代码" / sub).exists():
            (base / "01_代码" / sub).mkdir(parents=True, exist_ok=True)
        write_if_not_exists(gk, "")

    print("\n[01_代码/Algorithm 占位（按 topic）]")
    for algo in TOPIC_ALGO_FILES.get(topic, TOPIC_ALGO_FILES["generic"]):
        write_if_not_exists(base / "01_代码" / "Algorithm" / f"{algo}.h",
                            f"#ifndef __{algo.upper()}_H\n#define __{algo.upper()}_H\n\n/* TODO: declare API */\n\n#endif\n")
        write_if_not_exists(base / "01_代码" / "Algorithm" / f"{algo}.c",
                            f'#include "{algo}.h"\n\n/* TODO: implement */\n')

    print("\n[01_代码/tests]")
    write_if_not_exists(base / "01_代码" / "tests" / "algo_reference.py",
                        tmpl_algo_reference(year, code, name))
    write_if_not_exists(base / "01_代码" / "tests" / "cal_helper.py",
                        tmpl_cal_helper(year, code, name))
    write_if_not_exists(base / "01_代码" / "tests" / "README.md",
                        tmpl_tests_readme(year, code, name))

    print("\n[02_硬件]")
    write_if_not_exists(base / "02_硬件" / "材料清单_BOM.md", tmpl_bom())
    write_if_not_exists(base / "02_硬件" / "电路设计说明.md", tmpl_circuit_doc())

    print("\n[03_报告]")
    write_if_not_exists(base / "03_报告" / "设计报告.md", tmpl_design_report(year, code, name))
    write_if_not_exists(base / "03_报告" / "测试数据模板.md", tmpl_test_template())

    print("\n[04_调试记录]")
    write_if_not_exists(base / "04_调试记录" / "调试检查清单.md", tmpl_debug_checklist())
    write_if_not_exists(base / "04_调试记录" / "独到经验总结.md", tmpl_lessons_learned(year, code, name))

    print(f"\n✅ 完成。下一步：")
    print(f"   1. cd 进入 {base.relative_to(repo_root)}")
    print(f"   2. 完成 00_深度审题与方案论证.md（5 视角）")
    print(f"   3. 在 01_代码/tests/algo_reference.py 写算法 + 跑通 PC 验证")
    print(f"   4. 然后才进入 Drivers / Core")


def main():
    ap = argparse.ArgumentParser(description="新真题脚手架生成器")
    ap.add_argument("year", nargs="?", help="年份，如 2026")
    ap.add_argument("code", nargs="?", help="题号，如 A / B / H")
    ap.add_argument("name", nargs="?", help="题名（不带年份和题号），如 单相功率分析仪")
    ap.add_argument("--mcu", default="MSP430FR5994", help="主控 MCU 型号")
    ap.add_argument("--topic", default="generic",
                    choices=list(TOPIC_ALGO_FILES.keys()),
                    help="题目类型，决定算法占位文件")
    ap.add_argument("--list-topics", action="store_true", help="列出所有 topic")
    args = ap.parse_args()

    if args.list_topics:
        print("可用 topic:")
        for k, v in TOPIC_ALGO_FILES.items():
            print(f"  {k:8s} → 算法占位: {', '.join(v)}")
        return

    if not (args.year and args.code and args.name):
        ap.error("需要指定 year、code、name 或 --list-topics")

    repo_root = find_repo_root()
    generate(args.year, args.code, args.name, args.mcu, args.topic, repo_root)


if __name__ == "__main__":
    main()
