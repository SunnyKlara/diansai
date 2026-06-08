# {{YEAR}} · {{PROBLEM}} · 代码工程

> 模式：{{MODE}}（hardware / software / algo）

## 目录结构

```
01_代码/
├── Core/          # 业务/主循环/任务编排
├── Drivers/       # 硬件抽象 / IO 适配
├── Algorithm/     # 纯算法（与硬件解耦，可单测）
├── {{CONF_FILE}}  # 集中可调参数
└── README.md
```

## 编译

```bash
TODO：填入一行命令，例如 `make` / `cmake -B build && cmake --build build` / `python -m solution`
```

## 运行 / 烧录

TODO：写清楚怎么把可执行物投放到目标平台。

## 关键引脚 / 端口（硬件比赛）

| 模块 | 引脚 / 端口 | 备注 |
|---|---|---|
| TODO | TODO | TODO |

## 数据流 / 模块图

TODO：用文字描述或贴 mermaid 图。

## 已通过的指标

| 指标 | 设计目标 | 实测 | 状态 |
|---|---|---|---|
| TODO | TODO | TODO | TODO |

## 已知 Issue

- TODO
