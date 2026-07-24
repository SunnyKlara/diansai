# 算法层 PC 验证方法论 —— 总入口

> 这是从历年真题**审题分析** + 校赛B 真机经验沉淀的方法论（除校赛B 外，下列算法均未真机验证）。
> 核心信条：**算法 0 系统性误差是最低标准。误差预算全部留给硬件链路。**

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
├── snippets/                     # 跨题可复用算法参考（7 个 .py，各带 _example 自测）
│   ├── README.md
│   ├── pid_controller.py         # PID + anti-windup + ramp-up
│   ├── dft_harmonic.py           # 单频 DFT × N 谐波 + THD
│   ├── rms_meter.py              # 滑动窗口 RMS
│   ├── sync_sample.py            # 同步采样率计算
│   ├── flat_top_window.py        # 平顶窗 / Hann / Hamming
│   ├── dual_loop_pi.py           # 双闭环 PI
│   └── dcdc_simple_model.py      # DC-DC 平均模型
└── tools/
    ├── README.md                 # 元工具说明
    └── run_all_validation.py     # 一键回归（扫描 workbench/ + snippets 的 _example）
```

---

## 30 秒上手

### 拿到新题

按 `workbench/_工作台工作规范.md` 建题目目录，从 `snippets/` 拷所需算法参考到 `tests/` 起步。

### 改了某题算法

```bash
python library/通用能力/算法层PC验证方法论/tools/run_all_validation.py
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

## 验证状态（诚实标注）

除 `实战复盘/校赛B`（STM32H750 真机）外，本方法论的 snippets / 工具仅在 **PC 层自测通过**（`_example` 断言）；接省赛题后再补真机数据。别把 PC 验证当成真机成绩。

---

## 推荐阅读顺序

1. **新人**：先读 `HOWTO_新人入门.md`（30 分钟掌握全套方法论）
2. **要刷新题**：`tools/README.md` + `01_PC验证脚手架.md` + `snippets/README.md`
3. **要写 UART**：`02_UART调试协议规范.md`
4. **要找合成信号**：`03_合成信号工具集.md`
5. **想看实战例子**：`snippets/` 下的参考实现;真机工程算法层见 `实战复盘/校赛B/工程_STM32H750/Core/`

---

## 维护

修改本目录下任何文件都视为方法论升级。如果发现新真题暴露了模板缺失，应该：

1. 先在该真题的 `tests/` 中实现
2. 验证有效（一次以上成功使用）
3. 抽到本目录下作为模板
4. 更新 INDEX 和相关 .md
