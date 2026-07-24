# 算法层 PC 验证 - 工具

```
tools/
└── run_all_validation.py   # 一键回归：扫描 workbench/ 下的 algo_reference.py + snippets 的 _example
```

## run_all_validation.py — 一键回归

**核心价值**：改一行算法就能确认所有 PC 验证用例仍然通过。

```bash
python run_all_validation.py                 # 全部跑
python run_all_validation.py --filter 2026   # 只跑匹配的
python run_all_validation.py --verbose       # 完整输出
```

**扫描范围**：`workbench/**/tests/algo_reference.py` + `snippets/*.py` 的 `_example`。

> 当前 workbench 尚无算法回归用例（校赛B 用 STM32H750、不在本 PC 验证体系内），**基线待省赛题接入 `algo_reference.py` 后建立**。

## 配套自动化（可选）

在 `.kiro/hooks/` 加一个 fileEdited hook，监听 `algo_reference.py`：

```json
{
  "name": "Algo PC Validation Auto Run",
  "version": "1",
  "when": { "type": "fileEdited", "patterns": ["**/tests/algo_reference.py"] },
  "then": { "type": "runCommand", "command": "python library/通用能力/算法层PC验证方法论/tools/run_all_validation.py" }
}
```

改算法自动跑一遍回归，**改坏立刻知道**。
