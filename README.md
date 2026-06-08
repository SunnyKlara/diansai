# 全国大学生电子设计竞赛 备赛仓库 / diansai

> 系统化备赛资产库  
> 目标：**拿到近五年任意一道真题，照抄文件就能从买料到比赛跑通**

仓库地址：<https://github.com/SunnyKlara/diansai>

[![Stars](https://img.shields.io/github/stars/SunnyKlara/diansai?style=social)](https://github.com/SunnyKlara/diansai/stargazers)
[![Last Commit](https://img.shields.io/github/last-commit/SunnyKlara/diansai)](https://github.com/SunnyKlara/diansai/commits/main)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![Contribute](https://img.shields.io/badge/contributing-welcome-brightgreen)](CONTRIBUTING.md)
![Topic](https://img.shields.io/badge/topic-NUEDC-blue)
![Topic](https://img.shields.io/badge/topic-电赛-red)
![Topic](https://img.shields.io/badge/MCU-STM32%20%7C%20MSPM0%20%7C%20MSP430-green)
![Status](https://img.shields.io/badge/24题-100%25-brightgreen)
![Status](https://img.shields.io/badge/S级题-5%2F5-gold)

---

## 🏆 当前状态（2026-05）

- ✅ **近五年（2021–2025）24 道真题**全部完成"必备六件套"
- ✅ **S 级 5 道标杆题**完成"完全可复刻包"（**15 项资产 / 题**：引脚表 / 接线表 / **万用板布局图** / 采购单 / SysConfig / 故障树 / 复刻指南）
- ✅ **A 级 5 道**升级到 + 引脚表 + 接线表
- ✅ **5 题驱动**全部升级到真实 HAL / driverlib，0 错误编译
- ✅ **5 题 PC 端 Python 金标准** 100% 覆盖（spwm / control / fft / dft / pid / svpwm 全部可 PC 跑通）
- ✅ **第一梯队工具箱**：横向决策对照、PID 调参表、赛前 7 天脚本、工具箱清单、答辩 30 问、降级清单、**5 题 FAQ 高频踩坑**
- ✅ **赛场实战模板**：答辩 PPT 16 页 + 设计报告八段式 docx 模板 + **评委隐性评分细则**
- ✅ **算法层合规**：156 个 .c 文件 0 错误，0 处真违规（HAL/driverlib 不入算法层）

## 🎯 数字化指标

| 指标 | 数值 |
|---|---|
| 真题总数 | **24 道**（2021~2025 全覆盖）|
| 标杆题 S 级 | **5 道** × 15 项资产 = 75 文件 |
| A 级题 | **5 道** × 8 项 = 40 文件 |
| .c 文件总数 | **156 个**（全部 0 错误）|
| .h 文件总数 | **147 个**（全部 include guard）|
| Python 金标准 | **10 个**（5 题 × 2 文件）|
| 赛场工具箱 | **12 个 markdown** |
| 总 markdown 数 | **300+ 个** |

---

## 🚀 新人入门路径

### 第一次来？

**👉 先读 [ARCHITECTURE.md](ARCHITECTURE.md) — 项目地图，搞清每个目录干什么**

或者按以下顺序也行：

1. **[cases/_第一梯队总览.md](cases/_第一梯队总览.md)** —— 5 题导航 + 选题
2. **[skills/完全复刻包标准.md](skills/完全复刻包标准.md)** —— 一题完整资产应该长啥样
3. 选一道 S 级题 → 打开 `05_完整复刻指南.md` —— 4 周端到端流程

### 想动手刷一道？

```
1. 选题（看 _第一梯队总览.md 的"选哪道刷"）
   └─ 推荐 2024H 自动行驶小车（最容易跑通）

2. 打开 cases/2024/H_自动行驶小车/05_完整复刻指南.md
   按 Day-by-Day 走 4 周

3. 出问题翻 04_调试记录/故障决策树.md
```

---

## S 级 5 题（完全可复刻包）

| 题号 | 题目 | 方向 | 平台 | 评分 | 难度 |
|---|---|---|---|---|---|
| **2024 H** | [自动行驶小车](cases/2024/H_自动行驶小车/) | 控制 | MSPM0G3507 | 37/40 | ★★★ |
| **2024 B** | [单相功率分析仪](cases/2024/B_单相功率分析仪/) | 仪表/低功耗 | MSP430FR5994 | 38/40 | ★★★ |
| **2025 G** | [电路模型探究装置](cases/2025/G_电路模型探究装置/) | 仪表/DSP | STM32F407 | 38/40 | ★★★ |
| **2023 A** | [单相逆变器并联](cases/2023/A_单相逆变器并联/) | 电源 | STM32G474 | 37/40 | ★★★★ |
| **2025 A** | [能量回馈变流器](cases/2025/A_能量回馈变流器/) | 电源 | STM32G474 | 37/40 | ★★★★★ |

每题包含：
- `00_深度审题与方案论证.md` / `00_做题指南.md`
- `01_代码/`：config.h + Core/Drivers/Algorithm 三层 + **真实 HAL 0 错误** + 引脚分配表 + CubeMX/SysConfig 配置说明 + tests/ Python 金标准
- `02_硬件/`：BOM + **可执行采购清单（含淘宝关键词）** + 电路设计说明 + **接线表（每根杜邦线一行）** + **万用板布局图（板布局 + 接地策略 + 散热 + 走线）**
- `03_报告/设计报告.md`（八段式）
- `04_调试记录/`：调试清单 + 经验总结 + **故障决策树（5 分钟定位）**
- `05_完整复刻指南.md`：4 周端到端 + 自评清单

## B 级 19 题（必备六件套齐全）

剩余 19 道（2021/2022 全部 + 2023/2024/2025 部分）已完成**必备六件套**（深度审题 / 代码三层 / BOM / 电路 / 报告 / 经验），可作刷题基础。详见 [_进度看板.md](cases/_进度看板.md)。

---

## 仓库结构（五层能力框架）

> 详细说明见 [ARCHITECTURE.md](ARCHITECTURE.md)。

```
diansai/
├── ARCHITECTURE.md   ← 项目地图与框架（先读）
├── skills/           ← ① 技能：方法论 SOP（刷题工作流 / 报告 / 答辩）+ 系统总纲
├── cases/            ← ② 案例：24 道历年真题 + 2026 校赛 B（每题六件套）
│   ├── 2021/ 2022/ 2023/ 2024/ 2025/ 2026/校赛B_智能球平衡控制装置/
│   ├── _进度看板.md         ← 刷题状态（唯一真相源）
│   └── _PID实战调参表.md 等共享工具
├── library/          ← ③ 积木：可复用零件（被 cases 引用）
│   ├── 方向训练/     ← 8 大技术方向
│   ├── 通用能力/     ← PID / FFT / STM32 / 驱动 / 算法层PC验证 / 报告撰写
│   └── 物资准备/     ← 分方向物资清单
├── workbench/        ← ④ 工作台：正在攻的题（校赛B_智能球平衡控制装置/）
├── web/              ← ⑤ 展示：对外刷题网页（移动端可访问）
├── 赛题/             ← 官方真题原始 PDF（只读）
├── 历史工程/         ← 2024 C 题实战工程参考（只读）
└── _archive/         ← 旁支隔离：跨赛事方法论 + 模拟题
```

---

## 五层模块

| 层 | 作用 |
|---|---|
| **skills/** | AI 怎么做题的方法论（即学即用），平时与赛时都遵循 |
| **cases/** | 历年真题资产 = AI 的参照库；新增内容默认进这里 |
| **library/** | 跨题复用的积木（算法 / 驱动 / 物资 / 骨架）|
| **workbench/** | 当前在攻的题，完成后沉淀回 cases |
| **web/** | 对外展示窗口，读取 cases 渲染，移动端访问 |

---

## 强制约束（Steering Always 注入）

详见 [.kiro/steering/电赛备赛仓库规范.md](.kiro/steering/电赛备赛仓库规范.md)

核心规则：

1. **真题目录强制结构**：`00_深度审题 / 00_做题指南 / 01_代码 (Core/Drivers/Algorithm + config.h) / 02_硬件 (BOM + 电路) / 03_报告 / 04_调试记录`
2. **算法层无 HAL 依赖**：必须可在 PC 端单独编译验证
3. **可调参数集中 config.h**：禁止散落在 .c 文件
4. **不直接 push main**：必须 feature 分支 + PR
5. **commit 格式**：`<feat|fix|docs|refactor|test|chore>: <简述>`

## 用户核心交付契约

仓库已按以下契约完成（详见 [steering 规范](.kiro/steering/电赛备赛仓库规范.md)）：

- **唯一验收标准**：近五年所有真题按"最高标准刷题工作流"完成
- **AI 自主决策**：碰到问题自己解决，不停下等命令
- **批次提交节奏**：每完成 1 题 → feat commit + push + 合 main
- **文档可裁剪**：标杆题保持高密度；批量补题保证结构完整 + 可编译 + 有定量指标

---

## 快速链接

### 立即开始
- 🌟 [第一梯队总览](cases/_第一梯队总览.md)
- 🛠️ [完全复刻包标准](skills/完全复刻包标准.md)
- 📅 [赛前 7 天冲刺脚本](cases/_赛前7天冲刺脚本.md)

### 调试 / 上场
- ⚙️ [PID 实战调参表](cases/_PID实战调参表.md)
- 🔧 [比赛工具箱清单](cases/_比赛工具箱清单.md)
- 🚨 [赛场降级清单](cases/_赛场降级清单.md)

### 报告 / 答辩
- 📄 [设计报告八段式模板](skills/设计报告八段式模板.md)
- 🎤 [答辩 PPT 模板](skills/答辩PPT模板.md)
- ❓ [答辩 30 问速查卡](cases/_答辩30问速查卡.md)
- 🎯 [评委隐性评分细则](skills/评委隐性评分细则.md)
- 📚 [5 题 FAQ 高频踩坑](cases/_5题FAQ高频踩坑.md)
- 📊 [测试数据记录模板](skills/测试数据记录模板.md)

### 工程骨架（拿来即用）
- 🧰 [STM32G4 工程骨架](library/通用能力/STM32开发/STM32G4_工程骨架.md)
- 🧰 [MSPM0G3507 工程骨架](library/通用能力/TI_MSPM0开发/MSPM0G3507_工程骨架.md)

### 赛中实战
- 🕐 [赛中 4 天每小时排程](skills/赛中4天每小时排程.md)
- ✅ [赛前 24h 终检清单](skills/赛前24h终检清单.md)

### 流程 / 工作流
- 🔁 [最高标准刷题工作流](skills/最高标准刷题工作流.md)
- 🤖 [AI 协作工作流](library/通用能力/AI协作工作流.md)
- 📊 [横向决策对照表](cases/_横向决策对照表.md)

---

## 原则

- **不要只看分析，要动手做**
- 每道历年真题都按真实比赛来做
- 基本要求 100% 完成是拿奖底线
- 报告占 20 分（满分 120），不能敷衍
- **5 分钟原则**：故障 5 分钟修不好就降级，不要在一项上耗死

## 许可

仅供学习交流。提交前请确认无敏感数据、无第三方版权材料。
