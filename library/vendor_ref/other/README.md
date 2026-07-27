# other/ —— 收藏区：与当前车题不相干的方向

> **这个桶存在的理由**：这些项目**有价值，只是现在用不上**。方向已锁车题（2026-07-27 用户拍板，见坑库"方向预警被否决后闭嘴执行"），所以它们从"待读清单"里搬出来，进收藏区。
>
> **什么时候来翻这里**：
> - **题目公布后发现不是车题** → `maglev/` 是我们唯一有真机实战近亲的方向（校赛B），直接开工别从零找
> - **赛后 / 国赛前扩方向** → 三个子桶各自是一条完整的技术线
> - **写报告 / 找建模思路** → `maglev/review_article`、`maglev/stm32_thesis` 是完整的技术写作范本
>
> **不要做的事**：赛前把这里当作业读。**放进收藏区就是明确表态"现在不读"**，这是编排的一部分，不是懒。

---

## maglev/ —— 磁悬浮 / 悬浮球 / 平衡（9 个，最有价值的子桶）

**为什么单独一个子桶**：① 押题里**管内钢球电磁悬浮 30%，是控制方向概率最高的场景**（依据=负空间：2026 省赛清单砍掉 2025 国赛的激光瞄准套件、保留强化磁力套件）；② 它是 **`实战复盘/校赛B_智能球平衡控制装置/` 的直系近亲** —— 全仓库唯一真机跑通过的题。**黑天鹅真来了，这个子桶 + 校赛B 工程就是现成起跑线。**

> **诚实边界（重要，别高估）**：这 9 个**全是国外教学 / 毕设项目，不是电赛作品**，平台也不是 MSPM0（TI TM4C / STM32 / AVR / Arduino）。
> 价值在**被控对象和校赛B 同类**（电磁铁 + 位置反馈 + 单点不稳定平衡）——可看建模、观测器、PID 结构、报告写法。**代码不可直接移植。**
> 补检索缘由：中文关键词"磁悬浮/悬浮"被 Android"悬浮球/悬浮窗"污染，两轮零结果；换英文 `magnetic levitation ball PID` / `maglev hall sensor` 一次就出。

| 目录 | 上游 | 许可 | commit | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|
| `ball_observer/` | [shiivashaakeri/Control-Magnetic-Levitation-Ball](https://github.com/shiivashaakeri/Control-Magnetic-Levitation-Ball) | 无 | `b9c0072` (2023-02-14) | 3.7 MB | ⭐ **最贴校赛B**：悬浮球**建模 + PID**，再上**状态反馈 + 观测器**两阶段。我们校赛B 已经上了 α-β 观测器，这份可对照理论推导（德黑兰大学高级控制课程） |
| `ti_tm4c/` | [ussserrr/maglev](https://github.com/ussserrr/maglev) | 无 | `4c1b989` (2018-04-29) | 0.2 MB | **TI 平台**（TM4C123 Cortex-M4F）+ 霍尔反馈 + PID —— 最接近"TI MCU 做悬浮"的工程形态 |
| `hardware_pcb/` | [ussserrr/maglev-hardware](https://github.com/ussserrr/maglev-hardware) | 无 | `bd0ec28` (2018-04-07) | 0.1 MB | 上一条的**原理图 + PCB**（电磁铁驱动 + 霍尔前端）。要自己搭悬浮硬件时看它 |
| `stm32_thesis/` | [dvdvideo1234/SystemMaglevThesis](https://github.com/dvdvideo1234/SystemMaglevThesis) | **Apache-2.0** | `38c8807` (2019-05-13) | 36 MB | STM32 + PID 的**毕设全套（含论文）**，报告结构可参考。许可宽松、可引用 |
| `avr_hall/` | [flannelhead/avr-maglev](https://github.com/flannelhead/avr-maglev) | **GPL-2.0** | `2c194a1` (2015-08-10) | 0.1 MB | 最小干净实现：霍尔读值 → PID → 电磁铁功率。看"最短路径"长什么样。⚠️ GPL 传染性，只读思路 |
| `arduino_pid/` | [fsantagostinobietti/mymaglev](https://github.com/fsantagostinobietti/mymaglev) | 无 | `a805d6c` (2021-10-26) | 9.9 MB | Arduino 用连续 PWM 调电磁铁强度 |
| `fb_design/` | [hsm-0510/magnetic_levitation_feedback_control_system_design](https://github.com/hsm-0510/magnetic_levitation_feedback_control_system_design) | 无 | `14af2ce` (2024-12-31) | 0.2 MB | 反馈控制器设计报告（机 + 电 + 磁一体建模） |
| `review_article/` | [felipecacique/MagneticLevitation](https://github.com/felipecacique/MagneticLevitation) | 无 | `4d7736e` (2024-09-18) | **166 MB** | 螺线管 + 霍尔 + PID 的**深度技术复盘文章** —— 写作范式值钱（给答辩/报告当范本）。体积第三大 |
| `labkit/` | [Hansolini/Take-home-Maglev-lab](https://github.com/Hansolini/Take-home-Maglev-lab) | **MIT** | `c3e6c77` (2026-04-25) | **315 MB** | 控制工程**教学实验套件 + 实验指导书**（从基础到进阶）。**全部 30 个仓库里体积第一** |

## vision/ —— 视觉 + 云台 / 双主控（2 个）

**什么时候用**：题目要"看到目标再动"（视觉伺服 / 打靶 / 目标追踪），或需要"MCU 管底盘 + 另一颗管感知"的双主控架构。
> 车题的视觉链路参考在 **`../car/kit_k230_vision/`**（同款 MSPM0 + K230 两端骨架），**先看那个**；这里的两个是别的平台组合。

| 目录 | 上游 | 许可 | commit | 体积 | 内容 |
|---|---|---|---|---|---|
| `2025E_dualmcu/` | [wengqidaifeng/2025-e-smartcar](https://github.com/wengqidaifeng/2025-e-smartcar) | 无 | `3a1dc94` (2026-05-23) | **171 MB** | 2025-E **双主控**：MSPM0 管底盘 + STM32 管云台。体积第二大 |
| `2023E_movetrack/` | [MenHimChan/MoveTrackSys_TIcup_2023](https://github.com/MenHimChan/MoveTrackSys_TIcup_2023) | 无 | `ccb8cd8` (2024-03-26) | 38 MB | 2023-E **省二**，STM32 + OpenMV + 步进 FOC。已在 [`AI与开源资源精选`](../../通用能力/AI与开源资源精选.md) 收录过 |

## dsp/ —— 仪表 / 信号处理（2 个）

**什么时候用**：题目是仪表方向（测量 / 频谱 / 信号源）。对应 `CONTINUATION_GUIDE` 里那条 `⬜ 备 DSP 仪表骨架`（FFT / 同步采样，PC + 板都能验）。

| 目录 | 上游 | 许可 | commit | 体积 | 内容 |
|---|---|---|---|---|---|
| `signal_analyzer/` | [IllusionMZX/NEU-EEContest2025-SignalDev](https://github.com/IllusionMZX/NEU-EEContest2025-SignalDev) | **MIT** | `dd6de9d` (2025-08-25) | 10 MB | **MSPM0G3507** 简易信号分析仪 —— **同款芯片 + MIT 可引用**，是本桶最实用的一个 |
| `ad9851_dds/` | [WitBlue6/AD9851-AD9850-Driver](https://github.com/WitBlue6/AD9851-AD9850-Driver) | 无 | `2d7418c` (2024-10-24) | 0.3 MB | DDS 信号源驱动（MSPM0），仪表题的信号发生一侧 |

配套我们自己的：[`library/通用能力/FFT与信号处理/`](../../通用能力/FFT与信号处理/) + [`library/通用能力/算法层PC验证方法论/`](../../通用能力/算法层PC验证方法论/)（FFT 可在 PC 上先验完再上板）。

---

## 体积（收藏区占了总量的大头，知情即可）

本桶 13 个仓库实测 **750 MB**（占全部 30 仓 1376 MB 的 55%），其中前三个就占 652 MB：`maglev/labkit` 315 MB、`vision/2025E_dualmcu` 171 MB、`maglev/review_article` 166 MB。
**不删** —— 盘还有 1.2 TB，且删了就得重下（赛场没网）。真需要腾地方时按体积砍这三个，`.\_refresh.ps1 -SkipExisting` 随时拉回。
