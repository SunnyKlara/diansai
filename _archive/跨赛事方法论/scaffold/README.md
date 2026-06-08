# Scaffold · 一键脚手架

把整个 `scaffold/` 目录复制到任意"<比赛名>备赛仓库"根目录，跑一次脚本就能拿到完整的"必备六件套"空骨架。

## 依赖

- **Python 3.7+**（核心渲染由 `_render.py` 完成，跨平台跨编码最稳）
- Windows 上 `init_repo.bat` 会自动找 `py -3` 或 `python`
- macOS / Linux 用 `init_repo.sh`，可用 `PYTHON=python3.11` 指定解释器

## 用法

### Windows（cmd / 任意终端）

```bat
init_repo.bat --year 2024 --problem A_demo
init_repo.bat --year 2024 --problem B_kaggle --mode software --root 备赛系统\B_比赛实战
init_repo.bat --year 2024 --problem C_icpc   --mode algo
```

### macOS / Linux

```bash
./init_repo.sh --year 2024 --problem A_demo
./init_repo.sh --year 2024 --problem B_kaggle --mode software
./init_repo.sh --year 2024 --problem C_icpc   --mode algo
```

### 直接调 Python（最稳）

```bash
python _render.py --year 2024 --problem A_demo --mode hardware \
                  --root "备赛系统/B_历年真题实战" \
                  --templates ./_templates
```

## 参数

| 参数 | 必填 | 默认 | 说明 |
|---|---|---|---|
| `--year` | ✅ | - | 赛季年份，如 `2024` |
| `--problem` | ✅ | - | 题目代号 + 名称（推荐下划线分隔），如 `A_能量回馈变流器` |
| `--mode` | ❌ | `hardware` | `hardware` / `software` / `algo` 三选一 |
| `--root` | ❌ | `备赛系统/B_历年真题实战` | 真题根目录（相对仓库根） |

## 三种模式对应表

| 模式 | 适用 | `02_*/` 目录 | 配置文件 | 典型比赛 |
|---|---|---|---|---|
| `hardware` | 硬件实物比赛 | `02_硬件/材料清单_BOM.md` | `config.h` | 电赛、智能车、RM、嵌入式邀请赛 |
| `software` | 数据/AI 比赛 | `02_环境/依赖清单.md` | `config.yaml` | Kaggle、天池、飞桨、数模 |
| `algo` | 算法竞赛 | `02_环境/编译与评测说明.md` | `template.cpp` | ICPC、CCPC、蓝桥 |

## 产物（以 hardware 模式为例）

```
<root>/<year>/<problem>/
├── 00_题目原件.md
├── 00_深度审题与方案论证.md
├── 01_代码/
│   ├── Core/.gitkeep
│   ├── Drivers/.gitkeep
│   ├── Algorithm/.gitkeep
│   ├── config.h
│   └── README.md
├── 02_硬件/
│   └── 材料清单_BOM.md
├── 03_报告/
│   └── 设计报告.md
└── 04_调试记录/
    ├── 调试检查清单.md
    └── 独到经验总结.md
```

每个文件都已经是带 TODO 占位的可写模板，直接打开就开写。

## 设计取舍

- **核心用 Python**：因为 PowerShell 5.1 在 Windows 中文环境会用 ANSI 解析 `.ps1` 字符串，处理中文路径不可靠；Python 全程 UTF-8 安全。
- **bat / sh 是薄封装**：只负责参数解析和调用 Python，业务逻辑一律在 `_render.py`，单点改动就同时影响双平台。
- **模板文件名全英文**：`_templates/` 下所有源文件用纯 ASCII 名（`BRIEF_*`、`CODE_*`、`ENV_*`、`LOG_*`、`REPORT_*`），输出文件名才用中文，避开跨工具调用时编码丢失。
- **可重入**：已有的目标文件不会被覆盖，可以反复跑。
