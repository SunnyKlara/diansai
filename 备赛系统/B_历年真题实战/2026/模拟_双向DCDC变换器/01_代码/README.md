# 2026 模拟 双向DCDC变换器 — 代码工程说明

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

STM32G474

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
