# car/ —— 现役区：当前车题直接相关

> **什么时候来翻这里**：正在做车（循迹 / 航向 / 差速 / 里程 / 视觉伺服 / 外设点不亮）、赛中卡在某个具体环节时。
> **这是 6 个桶里唯一"赛前就该动"的**——其余四个是备查与收藏。
>
> ⚠️ **引脚一律以 [`工程事实SSOT.md §B`](../../../.kiro/steering/工程事实SSOT.md) 为准，不抄任何一个参考工程的引脚**（它们是 LaunchPad / 别家自制板，我们是天猛星载板）。
> ⚠️ **增益数值一律不抄**（只抄结构、比例、顺序）——我们的达标值全在 [`workbench/mspm0/car/config.h`](../../../workbench/mspm0/car/config.h)。
> ⚠️ **11 个仓库全部未编译未上板**，README/表格里的能力都是作者自述。

---

## 赛前只值得读一个

**`imu_yaw_mpu6050/`** —— 20 分钟，纯读，正打我们当前的堵点。
它的结构和我们 `attitude.c` 一模一样（yaw 独立积分 + 互补滤波 pitch/roll），但工程细节更全：**梯形积分**（误差 O(dt³) vs 我们的矩形 O(dt²)）、**dt 限幅**（LCD 拖慢主循环这坑我们踩过两次）、角速度死区 0.25dps、单帧增量限幅、±180° 折返、温漂自补偿。
⚠️ 它是 MPU6050 + I2C，**驱动层不可移植**；且它假定 yaw = gz，**我们实测重力落在 +Y、轴向未定 —— 定轴这一步必须自己在板上做，谁的代码都替不了**（方法见 `CONTINUATION_GUIDE §四`）。

---

## 同芯片 / 同题型车工程（6 个）

| 目录 | 上游 | 许可 | commit | 体积 | 拿它干嘛 |
|---|---|---|---|---|---|
| `2024H_abcuer/` | [abcuer/2024-NUEDC-H-TI_CAR](https://github.com/abcuer/2024-NUEDC-H-TI_CAR) | 无 | `179ea40` | 5 MB | 2024-H 车的**第二个独立实现**（和 `library/通用能力/TI_MSPM0开发/参考工程_2024H电赛车_MIT/` 对照看，能分开"这题的必然"与"某队的偏好"）。四个动作原语（陀螺直行 / 灰度 PID 循迹 / 编码器定距 / 节点触发）+ 两环**并列不嵌套**靠 `line_flag` 切换 + `TimeLimit` 超时保护 |
| `2024H_zlc_periph/` | [ZhijianLi2003/ZLC_MSPM0_Peripheral_Library](https://github.com/ZhijianLi2003/ZLC_MSPM0_Peripheral_Library) | **GPL-3.0** | `066cfb0` | 106 MB | 2024-H **国一**外设库。⭐ 真正值钱的是根目录 `Design_Report(2024_Question_H).pdf`（1 MB）——**一份拿了国一的报告实物**，给 `skills/03_报告答辩/` 当对照物。⚠️ GPL-3.0 有传染性，代码别往我们公开仓库里抄 |
| `kit_k230_vision/` | [2262727886-stack/mspm0g3507-car-kit](https://github.com/2262727886-stack/mspm0g3507-car-kit) | 无 | `81d44f3` | 8 MB | 同款 **MG310 电机**；**K230 视觉链路两端都有**（MSPM0 侧 UART 接收 + 舵机跟踪 / K230 侧 MicroPython 色块追踪）——我们最大短板"识别→串口→MCU 解析"这里有能照着搭的骨架。前置仍是"手上到底有没有相机模块" |
| `car26_k1l0m/` | [K1L0M/MSPM0G3507-26-](https://github.com/K1L0M/MSPM0G3507-26-) | **MIT** | `b61b916` | 124 MB | 26 电赛小车控制器方案，2026-07-26 仍在推。**MIT + 最新 ⇒ 真需要抄一段码优先看这个**（本桶唯一可合法引用的完整车工程） |
| `gyro_damp_thorn/` | [Thorn-ym/TI_Car](https://github.com/Thorn-ym/TI_Car) | 无 | `b4d8870` | 36 MB | ⭐ 一个概念值得记：**陀螺"横摆阻尼"= 用角速度做阻尼项压过弯震荡**，比"航向 PD"更省。另有直角弯专用状态机 + 分体三层硬件。⚠️ 描述里"可直接用于竞赛"是营销话术、无实测 |
| `gray_pid_5ee511/` | [5ee511/MSPM0G3507-Car](https://github.com/5ee511/MSPM0G3507-Car) | 无 | `f36d781` | 0.2 MB | 八路灰度直角弯：速度 PI + 巡线 PD 的**第三份对照**（和 `2024H_abcuer` 同构，三份一起看能看出哪种做法是共识） |

## MSPM0 外设驱动备查（3 个）

> 赛中"某个外设不会配"时的**第二参考**。**第一参考永远是本机 SDK + [`car/sdk_find.ps1`](../../../workbench/mspm0/car/sdk_find.ps1)**（能检索本机 1765 个官方 `.syscfg`，比翻别人的工程准）。

| 目录 | 上游 | 许可 | 体积 | 备注 |
|---|---|---|---|---|
| `periph_modules/` | [Torris-Yin/mspm0-modules](https://github.com/Torris-Yin/mspm0-modules) | 无 | 20 MB | **197★ 人气第一**，按 topic 分（mpu6050 / oled / vl53l0x / bno08x / i2c / spi）。三个里先看这个 |
| `periph_ex/` | [ZhiKong0/MSPM03507-module-examples](https://github.com/ZhiKong0/MSPM03507-module-examples) | 无 | 80 MB | 6551 文件的模块例程库，与上一个重叠。**目录名故意起得短**：它的最深文件已在 254 字符，离 Windows 260 上限只剩 6 格，改名前先量（见 `_refresh.ps1` 顶部 PATH BUDGET） |
| `periph_driverlib/` | [dzzz-qcxf-studio/MSPM0_Driver_Lib](https://github.com/dzzz-qcxf-studio/MSPM0_Driver_Lib) | 无 | 0.1 MB | 声明"基于天猛星、从跑通项目提取"，但**实测只有 21 个文件**，内容量很小 ⇒ 最低优先 |

## 四驱（赛后线，不是赛前）

| 目录 | 上游 | 许可 | 体积 | 备注 |
|---|---|---|---|---|
| `chassis_4wd/` | [xy1092/mspm0g3507-4wd-template](https://github.com/xy1092/mspm0g3507-4wd-template) | 无 | 0.3 MB | 双 TB6612 + ICM4568x + FreeRTOS 的四驱模板 → 对 [`四驱改版_接线设计.md`](../../../workbench/天猛星主板平台/四驱改版_接线设计.md)（`待打板+真机验证`）有参考价值。**赛前不碰**：3 天回不来板，比赛硬件就是现 2WD；且它用 RTOS、我们裸机 SysTick 5kHz，调度模型不同 |

---

## 我们比这些都强的地方（别照搬回来）

- **编码器定时采样 4x 正交解码抗 EMI** —— 翻遍本目录 11 个工程，**没有一个这么做**（全是边沿中断或硬件 QEI）。这是我们真机换来的独有资产（坑库"采样解码 > 上拉治 EMI"），**不要换回边沿中断**。
- **速度环 / 位置环 / 差速层已真机达标**（`4bbaae5` / `3a4c6f5` / `0c0130d`），别拿未验证的外部实现替换已验证的自家实现。
