# 外部参考仓库镜像 · 索引（vendor_ref）

> **这个目录是什么**：把 [`../通用能力/TI_MSPM0开发/MSPM0电赛开源项目清单_2026.md`](../通用能力/TI_MSPM0开发/MSPM0电赛开源项目清单_2026.md) 索引到的外部仓库，**全部 shallow clone 到本地**，为的是**赛场没网也能查**。
>
> **入库规则（看清楚，别误会）**：本目录**只有 `00_INDEX.md` + `_refresh.ps1` 两个文件进 git**，1.38GB 的第三方代码本体全部 `.gitignore` 掉。
> 原因是合规不是洁癖 —— 下表 `许可` 列为 `无` 的仓库**没有 LICENSE 文件**，我们这个仓库是公开的，**无权替他们再分发**。本地留副本读思路是合规的。
>
> **重建镜像**（换机器 / 清过盘 / 队友要同一份）：
> ```powershell
> cd library/vendor_ref
> .\_refresh.ps1                 # 缺的克隆，已有的更新
> .\_refresh.ps1 -SkipExisting   # 只补缺的（上次部分失败后用这个）
> ```
>
> **诚实边界**：下表的 `commit`/`日期`/`许可`/`体积` 是**我在本机实测**的（`git rev-parse` + 读 LICENSE 首行 + 递归统计）。
> 但 **30 个仓库我一个都没编译、没上板、没复现任何指标** —— 它们的能力声明一律是作者自述，见清单文档里的证据分级。**别把任何一条当"已验证"往我们文档里抄。**

---

## 0. 赛场只看三个（其余是备查，不是作业）

| 顺序 | 看哪个 | 干什么用 | 花多久 |
|---|---|---|---|
| 1 | `t2_nuedc_topic/` | **1994–2026 历年真题原件**。赛中"这题像哪年哪道"秒查；`Classification.xlsx` 是全量题目分类表 | 查一次 1 分钟 |
| 2 | `t2_mpu6050_yaw/` | yaw 独立积分 + 梯形积分 + 六道保护，**对照我们 `attitude.c` 查漏项**（`dt` 限幅这条我们踩过两次） | 读 20 分钟 |
| 3 | `t4_mspm0_modules/` | 197★ 模块驱动集。赛中"某外设不会配"时的**第二参考**（第一参考永远是本机 SDK + `car/sdk_find.ps1`） | 按需 |

> ⛔ **别做的事**：赛前把这 30 个逐个读通，或试图把别人的 BSP 层 merge 进 `workbench/mspm0/car/`。我们的扁平工程虽然违反 `workbench/_工作台工作规范 §2`，**但它是真机跑通的**，赛前动架构 = 拿唯一能跑的资产去赌。

---

## 1. ⭐ 本次克隆的最大意外收获：题库里有 **2026 年同年省赛真题**

`t2_nuedc_topic/2026/4月(吉林)/` —— **9 份官方题目 PDF**（清单文档原以为题库只到 2025）：

| 题号 | 题名 | 与我们的关系 |
|---|---|---|
| A | 简易扫频仪 | 仪表方向 |
| B | 智能芯片盒 | — |
| C | 自动烤肠机 | 控制+温度 |
| **D** | **智能寻迹打靶协同系统** | **循迹 + 打靶 + 多机协同** |
| **E** | **轮式智能车** | **正面车题** |
| **F** | **直流电机协同** | **电机控制（我们的速度环/位置环主场）** |
| G | 简易可见光通信装置 | 通信方向 |
| H | 汉诺塔问题 | 算法+执行机构 |
| I | 简易液体容器监控装置 | 仪表/传感 |

**为什么这比历年趋势硬**：这是**同一年**（2026）另一个省的官方省赛整套题，9 道里有 **3 道压在电机/车/循迹**上。
`t2_nuedc_topic/2025/` 里还有 `2025器件清单.pdf` + `0_2025竞赛题目列表.xlsx`，可以和我们 `workbench/26材料清单素材/` 的 2026 真清单做**逐年器件对比**（押题文档 `05_历年演变与技术储备深挖.md` 当时是"推断"，现在有原件可核）。

**另一件对审题/押题直接有用的：`t2_nuedc_topic/Classification.xlsx`（15KB）= 「年份 × 题型类别」矩阵。**
实测规模 **21 行 × 14 列**（`A1:N21`），类目 8 个：**仪器类 / 通信类 / 测控类 / 电源类 / 信号处理 / 控制类 / 飞行器 / 高频及通信类**；逐年逐题打标（单元格是 `题号_题名`，如 `B_风力摆控制系统`、`B_滚球控制系统`、`C_坡道行驶电动小车`），另有一列 **备注** 记编者对模糊题的判断（例：某年 C 题标"重点其实在无线充电，介于电源与控制之间"；另有"A 题、G 题不太清楚分类"）。
⇒ **比我们 `05_历年演变与技术储备深挖.md` 靠 narratives 推断的"两大家族"覆盖更全**，那份当时已诚实标注"非逐年原件比对"——现在可以拿这张表核。
⚠️ **两条诚实边界**：① **这张表是题库仓库作者维护的，不是官方文件**（同目录下的题目 PDF 才是官方原件）——当"社区整理"用，别当权威判据；② **我只解出了字符串表与表格维度，没有逐格解开成年份↔类别的完整映射**（`待核`）。要看直接用 Excel 打开即可，**不必再转一份 md 进仓库**（多一份副本 = 多一处漂移）。

> **⚠️ 诚实标注**：吉林 4 月省赛 ≠ 我们 7-29 那场，各省自主命题，**这不是"押到题"**，只是同年同赛事的强参照。要不要据此调整 `02_押题_场景概率排序.md` 的概率，**等你拍板**——我不擅自改押题结论，也不再单开分析文档（`CONTINUATION_GUIDE` 已定"停止再产分析文档"）。

---

## 2. 全部 30 个镜像（本机实测数据 · 2026-07-27）

**许可列读法**：`MIT`/`Apache-2.0` = 保留原作者声明即可引用；`GPL-2.0`/`GPL-3.0` = **可抄但有传染性**，抄进我们公开仓库的固件会要求整体同许可开源，**赛前别碰**；`无` = 无 LICENSE 文件，**只读思路、不抄码**（要抄先去 issue 问授权）。

### T1 有赛绩背书 + 同款 MSPM0G3507

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t1_zlc_peripheral_lib` | [ZhijianLi2003/ZLC_MSPM0_Peripheral_Library](https://github.com/ZhijianLi2003/ZLC_MSPM0_Peripheral_Library) | **GPL-3.0** | `066cfb0` | 2026-04-15 | 106 MB | 2024-H 国一外设库。⭐ 真正值钱的是根目录 `Design_Report(2024_Question_H).pdf` —— **一份拿了国一的报告实物**，给 `skills/03_报告答辩/` 当对照物 |
| `t1_abcuer_2024H_car` | [abcuer/2024-NUEDC-H-TI_CAR](https://github.com/abcuer/2024-NUEDC-H-TI_CAR) | 无 | `179ea40` | 2026-07-11 | 5.2 MB | 2024-H 车的**第二个独立实现**（与 `参考工程_2024H电赛车_MIT/` 对照看，能分开"这题的必然"和"某队的偏好"）。它的 `TimeLimit` 超时保护 = 我们 🔴 待补的"运动指令固件侧时限自停" |
| `t1_car_kit` | [2262727886-stack/mspm0g3507-car-kit](https://github.com/2262727886-stack/mspm0g3507-car-kit) | 无 | `81d44f3` | 2026-06-08 | 7.8 MB | 同款 MG310 电机；**K230 视觉链路两端都有**（MCU 侧 UART 接收 + K230 侧 MicroPython）；自带 `.claude/skills/` 技能包 |

### T2 单点打靶，正对当前堵点

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t2_nuedc_topic` | [CCBP/NUEDC_Topic](https://github.com/CCBP/NUEDC_Topic) | 无（题目为官方公开件） | `7e6a24a` | 2026-07-01 | 49 MB | **⭐ 赛场离线题库 1994–2026** + `Classification.xlsx` 分类表。见 §1 |
| `t2_nuedc_topic_backup` | [zuoliangyu/NUEDC_TOPIC](https://github.com/zuoliangyu/NUEDC_TOPIC) | 无 | `f75c21f` | 2026-03-24 | 113 MB | 题库第二源（体积更大、更新到 2026-03）。**盘紧的话这个可以删** |
| `t2_mpu6050_yaw` | [monokumakuki/2026TI_MPU6050_Yaw](https://github.com/monokumakuki/2026TI_MPU6050_Yaw) | 无 | `19aa24a` | 2026-07-23 | 9.0 MB | **赛前唯一值得读代码的一个**。结构和我们 `attitude.c` 同构但工程细节更全（梯形积分 / dt 限幅 / 死区 / 单帧增量限幅 / 温漂自补偿）。⚠️ 它是 MPU6050+I2C，驱动层不可移植；且它假定 yaw=gz，**我们的定轴必须自己在板上做** |
| `t2_control_topic_code` | [menoking/NUEDC-ControlTopic-Code](https://github.com/menoking/NUEDC-ControlTopic-Code) | 无 | `19a5e6f` | 2026-05-21 | 72 MB | 声明覆盖 2021–2025 控制类题代码。**0★、无 README，可信度未知** ⇒ 赛后翻 |

### T3 Agent Skill 同类项（看"怎么组织规则"，不看代码）

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t3_3507skill_Dvoid128` | [Dvoid128/3507-skill](https://github.com/Dvoid128/3507-skill) | **MIT** | `429a957` | 2026-07-26 | 0.3 MB | ⭐ 和我们 `skills/00_AI协作备赛操作系统.md` 同物种的独立实现。两个概念值得偷：**"分段路径脚本表——换题只改表"** + **"精度指标基准表"**（我们 skills 缺"做到多少算及格"）。⚠️ 它的 2026 清单解读**必须和 `workbench/26材料清单素材/` 官方转录交叉核对**（我们因拿错清单源翻车过一次） |
| `t3_mspm0skill_Ibook000` | [Ibook000/mspm0-skill](https://github.com/Ibook000/mspm0-skill) | 无 | `a4bb670` | 2026-07-26 | 1.1 MB | 同名不同人，覆盖 G3507/G3519，与上游高度重叠 |
| `t3_mspm0skill_Lmysang` | [Lmy-sang/MSPM0G3507-SKILL](https://github.com/Lmy-sang/MSPM0G3507-SKILL) | **MIT** | `b892fb7` | 2026-07-26 | 2.9 MB | 面向轮趣科技（WHEELTEC）板，板子不同 |

> 上游 [mc3545dada/mspm0-skill](https://github.com/mc3545dada/mspm0-skill)（MIT）**已 vendor 在 `.kiro/skills/mspm0-ccs/`**，不在本目录重复。

### T4 驱动 / 模板类（赛场离线备查）

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t4_mspm0_modules` | [Torris-Yin/mspm0-modules](https://github.com/Torris-Yin/mspm0-modules) | 无 | `66dc0f6` | 2025-07-13 | 20 MB | 197★ 人气第一。mpu6050 / oled / vl53l0x / bno08x / i2c / spi 分 topic |
| `t4_K1L0M_26car` | [K1L0M/MSPM0G3507-26-](https://github.com/K1L0M/MSPM0G3507-26-) | **MIT** | `b61b916` | 2026-07-26 | 124 MB | **真要抄一段码优先看这个**（MIT + 仍在推）。26 电赛小车控制器方案 |
| `t4_module_examples` | [ZhiKong0/MSPM03507-module-examples](https://github.com/ZhiKong0/MSPM03507-module-examples) | 无 | `7b4ec2e` | 2026-07-25 | 80 MB | 6551 文件的模块例程库，与 `t4_mspm0_modules` 重叠 |
| `t4_thorn_ti_car` | [Thorn-ym/TI_Car](https://github.com/Thorn-ym/TI_Car) | 无 | `b4d8870` | 2026-07-24 | 36 MB | ⭐ 一个概念值得记：**陀螺"横摆阻尼"= 用角速度做阻尼项压过弯震荡**，比"航向 PD"更省。⚠️ 描述里"可直接用于竞赛"是营销话术、无实测 |
| `t4_4wd_template` | [xy1092/mspm0g3507-4wd-template](https://github.com/xy1092/mspm0g3507-4wd-template) | 无 | `e9ca868` | 2026-07-22 | 0.3 MB | 四驱模板（双 TB6612 + ICM4568x + FreeRTOS）→ 参考 `四驱改版_接线设计.md`。**四驱是赛后线**（3 天回不来板），且它用 RTOS、我们裸机 SysTick 5kHz |
| `t4_5ee511_car` | [5ee511/MSPM0G3507-Car](https://github.com/5ee511/MSPM0G3507-Car) | 无 | `f36d781` | 2026-07-16 | 0.2 MB | 八路灰度直角弯，速度 PI + 巡线 PD 的第三份对照 |
| `t4_mspm0_driver_lib` | [dzzz-qcxf-studio/MSPM0_Driver_Lib](https://github.com/dzzz-qcxf-studio/MSPM0_Driver_Lib) | 无 | `ec285c1` | 2026-07-16 | 0.1 MB | 声明"基于天猛星、从跑通项目提取"。**实测只有 21 个文件 0.11MB，内容量确实很小**（清单当时的"存疑"已被证实） |

### T5 其他方向（车题外，留着防黑天鹅）

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t5_signal_dev` | [IllusionMZX/NEU-EEContest2025-SignalDev](https://github.com/IllusionMZX/NEU-EEContest2025-SignalDev) | **MIT** | `dd6de9d` | 2025-08-25 | 10 MB | MSPM0G3507 简易信号分析仪 → 对应 `⬜ 备 DSP 仪表骨架` |
| `t5_2025e_smartcar` | [wengqidaifeng/2025-e-smartcar](https://github.com/wengqidaifeng/2025-e-smartcar) | 无 | `3a1dc94` | 2026-05-23 | 171 MB | 2025-E 双主控：MSPM0 底盘 + STM32 云台 |
| `t5_movetrack_2023E` | [MenHimChan/MoveTrackSys_TIcup_2023](https://github.com/MenHimChan/MoveTrackSys_TIcup_2023) | 无 | `ccb8cd8` | 2024-03-26 | 38 MB | 2023-E 省二，STM32+OpenMV+步进 FOC |
| `t5_ad9851_driver` | [WitBlue6/AD9851-AD9850-Driver](https://github.com/WitBlue6/AD9851-AD9850-Driver) | 无 | `2d7418c` | 2024-10-24 | 0.3 MB | DDS 驱动（MSPM0） |

### T6 磁悬浮 / 悬浮球 —— 押题概率最高一档（30%）、校赛B 直系近亲

> **补上清单里那笔"未完成的检索"**：原来两轮中文检索被 Android "悬浮球/悬浮窗" 污染、零有效结果；换英文关键词（`magnetic levitation ball PID` / `maglev hall sensor`）一次就出。
> **诚实边界**：这批全是**国外教学/毕设项目，不是电赛作品**，平台也不是 MSPM0（TI TM4C / STM32 / AVR / Arduino）。价值在**被控对象和校赛B 同类**（电磁铁 + 位置反馈 + 单点不稳定平衡），可看建模、观测器、PID 结构、以及"这类系统怎么写报告"。**代码不可直接移植。**

| 目录 | 仓库 | 许可 | commit | 日期 | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|---|
| `t6_maglev_ball_observer` | [shiivashaakeri/Control-Magnetic-Levitation-Ball](https://github.com/shiivashaakeri/Control-Magnetic-Levitation-Ball) | 无 | `b9c0072` | 2023-02-14 | 3.7 MB | ⭐ 最贴校赛B：**悬浮球建模 + PID → 再上状态反馈 + 观测器**两阶段。我们校赛B 已经上了 α-β 观测器，这份可对照理论推导（德黑兰大学高级控制课程作业） |
| `t6_maglev_ti_tm4c` | [ussserrr/maglev](https://github.com/ussserrr/maglev) | 无 | `4c1b989` | 2018-04-29 | 0.2 MB | **TI 平台**（TM4C123 Cortex-M4F）+ 霍尔反馈 + PID，最接近"TI MCU 做悬浮"的工程形态 |
| `t6_maglev_hardware` | [ussserrr/maglev-hardware](https://github.com/ussserrr/maglev-hardware) | 无 | `bd0ec28` | 2018-04-07 | 0.1 MB | 上一条的**原理图 + PCB**（电磁铁驱动 + 霍尔前端）。要自己搭悬浮硬件时看这个 |
| `t6_maglev_stm32_thesis` | [dvdvideo1234/SystemMaglevThesis](https://github.com/dvdvideo1234/SystemMaglevThesis) | **Apache-2.0** | `38c8807` | 2019-05-13 | 36 MB | STM32 + PID 的**毕设全套**（含论文），报告结构可参考 |
| `t6_maglev_lab_kit` | [Hansolini/Take-home-Maglev-lab](https://github.com/Hansolini/Take-home-Maglev-lab) | **MIT** | `c3e6c77` | 2026-04-25 | 315 MB | 控制工程**教学实验套件**（含实验指导书）。最大的一个，**盘紧可删** |
| `t6_maglev_review` | [felipecacique/MagneticLevitation](https://github.com/felipecacique/MagneticLevitation) | 无 | `4d7736e` | 2024-09-18 | 166 MB | 螺线管 + 霍尔 + PID 的**深度技术复盘文章**（写作范式值钱）。**盘紧可删** |
| `t6_maglev_avr_hall` | [flannelhead/avr-maglev](https://github.com/flannelhead/avr-maglev) | **GPL-2.0** | `2c194a1` | 2015-08-10 | 0.1 MB | 最小干净实现：霍尔读值 → PID → 电磁铁功率。看最短路径长什么样 |
| `t6_maglev_arduino_pid` | [fsantagostinobietti/mymaglev](https://github.com/fsantagostinobietti/mymaglev) | 无 | `a805d6c` | 2021-10-26 | 9.9 MB | Arduino 连续 PWM 调电磁铁强度 |
| `t6_maglev_fb_design` | [hsm-0510/magnetic_levitation_feedback_control_system_design](https://github.com/hsm-0510/magnetic_levitation_feedback_control_system_design) | 无 | `14af2ce` | 2024-12-31 | 0.2 MB | 反馈控制器设计报告（机电磁一体建模） |

---

## 3. 体积 / 清盘

工作文件 **1376.5 MB / 30 仓库**；**含各仓 `.git` 元数据后磁盘实占 2.06 GB**（`--depth 1` 浅克隆，无完整历史）。盘紧时按体积砍前四个就够：
`t6_maglev_lab_kit`(315M) · `t5_2025e_smartcar`(171M) · `t6_maglev_review`(166M) · `t4_K1L0M_26car`(124M) · `t2_nuedc_topic_backup`(113M)。
删了随时 `.\_refresh.ps1 -SkipExisting` 拉回来。

**不能删的**：`t2_nuedc_topic`（赛场题库）· `t2_mpu6050_yaw`（对照 attitude.c）· `t4_mspm0_modules`（外设备查）。

---

## 4. 踩过的坑（已入本文，别重踩）

1. **PS 5.1 把无 BOM 的 UTF-8 脚本当 GBK 读** → `_refresh.ps1` 里写中文注释会把解析器炸掉（本次第一版就是这么死的）。**该脚本永远保持纯 ASCII**，中文说明放本文。
2. **一口气 30 个 clone 会大批失败**：首轮 19 成 11 败，而失败的 URL 紧接着用 `git ls-remote` 全部可达 ⇒ **是瞬时网络重置，不是仓库不存在**。已给脚本加 3 次重试 + 退避。**别看一次失败就下"仓库没了"的结论。**
3. **一个卡住的仓库会堵死整个队列**：`zuoliangyu/NUEDC_TOPIC` 在第 3 次重试上挂住（不报错、纯等），后面 15 个全排不上。已在脚本里设 `GIT_HTTP_LOW_SPEED_LIMIT=1000` / `GIT_HTTP_LOW_SPEED_TIME=30`，**低速 30 秒就中断**而不是无限等。
4. **许可列不能信别人的元数据**：清单文档写 ZLC 库"无 LICENSE"，实测**它有 LICENSE 且是 GPL-3.0**（传染性许可，性质完全不同）。`Dvoid128/3507-skill` 实测是 MIT、`SystemMaglevThesis` 是 Apache-2.0，清单都没标。**要判"能不能抄"就 clone 下来读 LICENSE 首行，别读 README。**

---

## 维护日志

- **2026-07-27**：创建。30 个仓库全部 clone 成功（`--depth 1`，1376.5MB）。相对清单文档的三处**实测修正**：① ZLC 库许可 `无` → **GPL-3.0**；② `3507-skill`=MIT、`SystemMaglevThesis`=Apache-2.0、`avr-maglev`=GPL-2.0、`Take-home-Maglev-lab`=MIT（清单未标）；③ `MSPM0_Driver_Lib` 的"内容量存疑"证实（21 文件 0.11MB）。**新增清单里没有的 T6 磁悬浮一组 9 个**（补上清单 §维护日志 自认"待补"的那笔检索）。**最大收获 = 题库含 2026 年 4 月吉林省赛 9 道真题原件 + 2025 器件清单原件**，见 §1。
