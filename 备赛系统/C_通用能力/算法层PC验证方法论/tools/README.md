# 算法层 PC 验证 - 工具集

这两个脚本是这套方法论的"元工具"：

```
tools/
├── run_all_validation.py   # 一键回归全部真题的 algo_reference.py
└── new_problem.py          # 新题脚手架生成器
```

## run_all_validation.py — 一键回归

**核心价值**：每改一行算法都能 1 秒内确认所有题目仍然通过。

```bash
# 全部跑
python run_all_validation.py

# 只跑 2024 年的
python run_all_validation.py --filter 2024

# 显示完整输出
python run_all_validation.py --verbose
```

**当前基线**（2026-05-26）：5 题全部通过，总耗时 0.53s。

## new_problem.py — 新题脚手架

**核心价值**：拿到新题 30 秒生成完整目录骨架，按方法论强制走"先 PC 验证再上 MCU"。

```bash
# 通用类
python new_problem.py 2026 X 题名

# 电源类（自动生成 svpwm/rms/voltage_loop 占位）
python new_problem.py 2026 A 双向 DC-DC --topic power

# 仪表类（自动生成 dft/rms/calibration 占位）
python new_problem.py 2026 B 智能电表 --topic measure --mcu MSP432P401R

# 控制类
python new_problem.py 2026 H 自动巡线 --topic control --mcu MSPM0G3507

# 信号处理类
python new_problem.py 2026 G 频谱分析 --topic signal --mcu STM32F407

# 列出 topic
python new_problem.py --list-topics
```

**生成内容**：
- 14 个 markdown / py 文件（标准目录结构）
- 算法层占位（带 .h / .c）
- 直接可跑的 algo_reference.py（输出"待填"占位）
- 直接可用的 cal_helper.py（标准 4 命令）

**生成后第一步**：编辑 `00_深度审题与方案论证.md` —— 不写完它**禁止**进入代码阶段。

## 配套自动化（推荐设置）

在 .kiro/hooks/ 下加一个 fileEdited hook，监听 `algo_reference.py` 修改：

```json
{
  "name": "Algo PC Validation Auto Run",
  "version": "1.0.0",
  "when": {
    "type": "fileEdited",
    "patterns": ["**/tests/algo_reference.py"]
  },
  "then": {
    "type": "runCommand",
    "command": "python 备赛系统/C_通用能力/算法层PC验证方法论/tools/run_all_validation.py"
  }
}
```

这样每次改算法都会自动跑一遍回归，**改坏立刻知道**。
