# 算法层 PC 验证方法论 —— 总入口

> 这是从 5 道电赛真题（2024B/H、2025A/G、2021A）实战中沉淀的方法论。
> 核心信条：**算法 0 系统性误差是最低标准。误差预算全部留给硬件链路。**
> 用 1 道 2026 模拟新题做了实战检验，方法论 + 脚手架 + snippets 全套打通。

---

## 文件清单

```
算法层PC验证方法论/
├── INDEX.md                      # 本文件，总入口
├── HOWTO_新人入门.md              # 30 分钟教队友学方法论
├── 00_算法层PC验证方法论.md       # 方法论核心（先读这个）
├── 01_PC验证脚手架.md             # 直接复制粘贴的 algo_reference / cal_helper 模板
├── 02_UART调试协议规范.md         # STAT / CAL / DUMP / RST 4 命令标准
├── 03_合成信号工具集.md           # 各类典型负载 / 滤波器的 Python 合成代码
├── 99_5题金标准实测数据.md        # 5 题的具体验证数值（基线）
├── snippets/                     # 跨题可复用代码片段（5 个 + README）
│   ├── README.md
│   ├── pid_controller.py         # PID + anti-windup + ramp-up
│   ├── dft_harmonic.py           # 单频 DFT × N 谐波 + THD
│   ├── rms_meter.py              # 滑动窗口 RMS
│   ├── sync_sample.py            # 同步采样率计算
│   └── flat_top_window.py        # 平顶窗 / Hann / Hamming
└── tools/
    ├── README.md                 # 元工具说明
    ├── run_all_validation.py     # 一键回归（5 真题 + 5 snippets + 1 模拟新题，1.22s）
    └── new_problem.py            # 新题脚手架生成器
```

---

## 30 秒上手

### 拿到新题

```bash
python 备赛系统/C_通用能力/算法层PC验证方法论/tools/new_problem.py 2026 A 题名 --topic measure
```

→ 生成完整目录骨架（00/01/02/03/04），算法 + 测试占位齐全。

### 改了某题算法

```bash
python 备赛系统/C_通用能力/算法层PC验证方法论/tools/run_all_validation.py
```

→ 1 秒内确认所有题目仍通过。**已挂 hook**，每次保存 `algo_reference.py` 自动跑。

### 现场出问题

```bash
# 装置端 UART：导出原始采样
python <题目>/01_代码/tests/cal_helper.py --port COM3 --dump out.csv

# PC 端：跑金标准对比
python <题目>/01_代码/tests/cal_helper.py --verify out.csv
```

→ 30 秒判断"算法 bug"还是"硬件 bug"。

---

## 方法论核心（强制流程）

```
拿到题 → 写深度审题 → 定算法接口（头文件）
              ↓
       写 algo_reference.py（Python 等价实现）
              ↓
       PC 验证 0 误差 ✓ ← 这一步通过才能上 MCU
              ↓
       写 Drivers / Core
              ↓
       UART DUMP 原始数据 → 喂 cal_helper.verify → 与金标准差 < 1e-4
              ↓
       校准互感器 / 传感器 → 实测精度
```

**红线**：步骤 4 不通过禁止上 MCU。算法层有 bug 时，到了 MCU 是双倍代价。

---

## 5 题成果一览（方法论的实战证明）

| 题目 | 算法层精度 | 题目要求 | 余量倍数 | cal_helper 特色 |
|---|---|---|---|---|
| 2024 B 单相功率分析仪 | 0.0008%（量化噪声底）| 1% | **1250×** | UART STAT/CAL/DUMP/RST 全套 |
| 2024 H 自动行驶小车 | 1.14s 完成 1m 直线 | 限 15s | **13×** | --simulate yaw 仿真 PID 参数 |
| 2025 A 能量回馈变流器 | SVPWM 100% 精度 | 0.25V | ∞ | --simulate 自动检测母线电压陷阱 |
| 2025 G 电路模型探究装置 | 增益误差 0.035 | 0.10 | **3×** | --plot Bode 图，--classify 类型识别 |
| 2021 A 信号失真度测量装置 | THD 误差 0.005% | 5%（基本）/ 3%（发挥） | **1000× / 600×** | 端到端 verify 已通过冒烟 |

---

## 复利

- 第一次刷题用 4 天
- 第二次套这套模板刷类似题用 1 天
- 跨届真题积累 5 套，2026 新题 80% 套路命中现成模板 → 节省 60% 时间

**这是电赛备战最大的复利来源**。

---

## 推荐阅读顺序

1. **新人**：先读 `HOWTO_新人入门.md`（30 分钟掌握全套方法论）
2. **要刷新题**：`tools/README.md` + `01_PC验证脚手架.md` + `snippets/README.md`
3. **要写 UART**：`02_UART调试协议规范.md`
4. **要找合成信号**：`03_合成信号工具集.md`
5. **想看实战例子**：`99_5题金标准实测数据.md` + 5 真题 + 1 模拟新题的 `tests/` 目录

---

## 维护

修改本目录下任何文件都视为方法论升级。如果发现新真题暴露了模板缺失，应该：

1. 先在该真题的 `tests/` 中实现
2. 验证有效（一次以上成功使用）
3. 抽到本目录下作为模板
4. 更新 INDEX 和相关 .md
