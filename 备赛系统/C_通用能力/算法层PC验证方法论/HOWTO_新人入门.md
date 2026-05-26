# 新人入门：30 分钟掌握"算法层 PC 验证"方法论

> 这是给队友的入门指南。看完能独立用方法论刷一道题。

---

## 你需要先理解的 3 件事

### 1. 为什么国一队伍快人一档？

不是因为他们更聪明，是因为他们的**调试反馈周期**短：
- 你：改一行代码 → 烧录 → 看 OLED → 改 → 再烧 = **5~10 分钟一轮**
- 他们：改一行 Python → `python algo_reference.py` = **1 秒一轮**

一天调参 8 小时：
- 你能调 96 轮
- 他们能调 28800 轮

**这是 300× 的差距**。

### 2. 算法和硬件必须分开调

电赛装置出问题，根因有 3 类：
- 算法 bug（逻辑错）
- 调参 bug（参数没收敛）
- 硬件 bug（焊接、采样、噪声）

如果三者混在 MCU 上，bug 定位**指数级困难**。

方法论的核心思想：**先把算法和调参在 PC 上彻底跑通，再上 MCU，剩下的全是硬件 bug**。

### 3. 金标准 = 长期资产

每写一个 Python 算法都是"长期资产"：
- 算法本身可以复用到下一题
- 出 bug 时它是"金标准"对照（MCU 输出 vs PC 输出，差 < 1e-4 才正确）
- 给报告"算法验证"章节提供数据

---

## 30 分钟实战：刷一道新题

### Step 1: 读题 + 写深度审题（10 分钟）

**禁止**：读完题就写代码。

**做**：复制脚手架，按 5 视角填 `00_深度审题与方案论证.md`：

```bash
python 备赛系统/C_通用能力/算法层PC验证方法论/tools/new_problem.py \
    2026 X 题名 --topic measure
```

5 视角是：
1. **题目精读**：分值 / 容差 / 隐含约束 / 评委怎么测
2. **误差预算**：每个误差源的量级 → 总误差 RSS → vs 容差
3. **MCU + 器件选型**：候选对比 + 关键参数
4. **软件架构**：状态机 + 算法分层
5. **硬件难点**：每个难点的对策

### Step 2: 定接口（5 分钟）

在 `01_代码/Algorithm/*.h` 里写函数声明（不写实现）。

例：

```c
void measure_thd(uint16_t *adc_data, uint16_t len, float fs, ...);
```

### Step 3: 写 Python 等价实现（10 分钟）

打开 `01_代码/tests/algo_reference.py`，按头文件写 Python 镜像。

**关键**：先去 `snippets/` 找现成的：

```python
import sys
from pathlib import Path
SNIPPETS = Path(__file__).resolve().parents[5] / \
    "C_通用能力" / "算法层PC验证方法论" / "snippets"
sys.path.insert(0, str(SNIPPETS))

from pid_controller import PIDController         # 控制题用
from dft_harmonic import DFTHarmonic              # 谐波 / THD 用
from rms_meter import RMSMeter                    # RMS 用
from sync_sample import calc_sync_rate            # 同步采样用
from flat_top_window import flat_top_window       # 非整周期窗用
```

如果 snippets 没有，就自己写。**写完检查：是否能抽到 snippets 给下一题用？**

### Step 4: 跑通 PC 验证（5 分钟）

```bash
python 01_代码/tests/algo_reference.py
```

要求：**理论误差 < 题目容差的 1/3**。

如果不达标，**绝对不要上 MCU**！调 PC 直到达标。

---

## 进入 MCU 阶段（PC 通过后）

### Step 5: 写 Drivers（半天）

**这是真正的硬件工程**。脚手架已经把 ADC / PWM / UART 占位写好了。

### Step 6: 写 main.c 状态机（半天）

复制 2024 B 的 main.c 改改即可（5 状态 + LPM）。

### Step 7: UART 调试 + cal_helper（关键）

实现 4 个标准命令：`STAT / CAL / DUMP / RST`。
PC 端用脚手架生成的 cal_helper.py 做调试。

### Step 8: 算法层金标准对比（关键）

```bash
# 装置 → PC：导出原始数据
python cal_helper.py --port COM3 --dump out.csv

# PC：跑金标准对比
python cal_helper.py --port COM3 --verify out.csv
```

如果 MCU 输出 vs PC 金标准差 > 1e-4，说明 MCU 端有移植 bug。

### Step 9: 校准 + 实测精度

```bash
python cal_helper.py --port COM3 --calibrate
```

输入标准基准（万用表 / 标准源），FRAM 永久保存系数。

---

## 反模式（千万别这么做）

### ❌ 反模式 1：跳过深度审题

"我看一眼就懂了，直接写代码"

→ 80% 的题目都比初看的复杂（隐含约束、评测细节、误差链）。**深度审题是国一的隐性技能**。

### ❌ 反模式 2：跳过 PC 验证

"算法很简单，直接 MCU 上写吧"

→ 你以为的"简单"，PC 验证会暴露 5 个边界 bug。MCU 上调要花 10× 时间。

### ❌ 反模式 3：Python 和 C 用不同算法

"Python 我用 numpy.fft，C 用 CMSIS-DSP"

→ 金标准就失效了。Python 必须**与 C 1:1 等价**（同样的循环结构、同样的累积顺序）。

### ❌ 反模式 4：把驱动逻辑写到算法里

"算法直接调 ADC 读"

→ 算法层应该**纯计算**（输入数组 → 输出结构）。驱动调用算法，反过来不行。

### ❌ 反模式 5：不抽 snippets

"这次写了 PID，下次重新写"

→ 跨题重复劳动。第三次出现就抽 snippet。

---

## 自检清单

刷一道题之前，回答这 8 个问题：

- [ ] 我有 `00_深度审题与方案论证.md`，5 视角都填了？
- [ ] 我的算法在 PC 上跑出 < 容差/3 的精度？
- [ ] 我有 `algo_reference.py` 作为长期金标准？
- [ ] 我的 C 代码和 Python 1:1 对应？
- [ ] 我实现了 STAT/CAL/DUMP/RST 4 个 UART 命令？
- [ ] 我有 `cal_helper.py` 配套调试？
- [ ] 我做了校准并写到 FRAM？
- [ ] MCU 输出和 PC 金标准差 < 1e-4？

8 个全 ✓ 才算"一道题完成"。

---

## 实战练习

### 练习 1：跑通 5 个真题的金标准（10 分钟）

```bash
python 备赛系统/C_通用能力/算法层PC验证方法论/tools/run_all_validation.py
```

应输出 `11/11 ALL PASSED`。

### 练习 2：在 2024 B 上跑端到端冒烟（5 分钟）

```bash
cd 备赛系统/B_历年真题实战/2024/B_单相功率分析仪/01_代码/tests
# （根据 README 里的端到端冒烟说明）
```

### 练习 3：用脚手架生成新题骨架（5 分钟）

```bash
python tools/new_problem.py 2026 测 测试题目 --topic generic
```

看看生成了什么 + 跑跑看。

---

## 有问题怎么办？

1. 先看 `00_算法层PC验证方法论.md`
2. 再看 `01_PC验证脚手架.md`（直接复制粘贴的代码）
3. 还不懂就模仿 `备赛系统/B_历年真题实战/2024/B_单相功率分析仪/`（最完整的参考样例）

---

## 终极信条

> **改一行 Python 比改一行 MCU C 代码快 1000 倍**。
> 所有能在 PC 上验证的，先在 PC 上验证。
> 上了 MCU 之后只剩硬件 bug。
