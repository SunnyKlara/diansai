# 2026 模拟 多点温度采集系统 — 算法层 PC 验证

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
