# 工程事实 · 单一真值源（SSOT）—— 天猛星 MSPM0 平台

> **为什么有本文**：引脚 / 已标定参数 / 烧录命令这类事实以前被抄进 `README` / `car.c` 注释 / `调试日志` / `guide` 多处，一改就留一串过时副本 → 下个对话拿旧值分析出错（本仓库已实际踩过：电流通道映射、ENC_CPR、速度环增益、剩余天数）。
> **铁律**：这些事实**只在本文登记**；其它文档**只指向本文、不复述具体值**。改任一事实 = **只改这一处** + 全仓 `git grep 旧值` 扫尾 + 更新该行 `校验于`。
> **性质**：always 注入（每个会话自带最新值）。**只登记"当前值"，不写推演过程**——推演进 `knowledge/决策与交接归档.md`，坑进 `knowledge/跨题坑库.md`。

---

## A. 自我真值（看源文件即最新，**任何文档都别写死数字**）

| 事实 | 真值 = 这个文件 |
|---|---|
| 引脚分配（唯一真值源） | [`workbench/天猛星主板平台/00_载板接线设计_天猛星平台.md`](../../workbench/天猛星主板平台/00_载板接线设计_天猛星平台.md) **§10.1** |
| 控制参数（PID/时基/限幅/标定值） | [`workbench/mspm0/car/config.h`](../../workbench/mspm0/car/config.h) |
| 电流通道映射 + 定标 | `workbench/mspm0/car/motor.c` 顶部注释（**按真机实测映射，非 syscfg 假设**） |
| 编码器解码方式/符号 | `workbench/mspm0/car/encoder.c` 顶部注释 |
| 芯片上跑的是哪版固件 | `git log` + 固件 boot 打印的 `build <__DATE__> <__TIME__>` |
| 四驱改版定版设计 | `workbench/天猛星主板平台/四驱改版_接线设计.md`（`待打板+真机验证`） |

> 提到参数请写"见 `config.h`"而非具体数字——本仓库已因在文档写死 `ENC_CPR=899`/`Kp0.03` 被真机标定打脸两次。

---

## B. 引脚事实（摘要 · 完整表以 §10.1 为准）

| 功能 | 引脚 | 校验于 |
|---|---|---|
| 电机 M1 IN1/IN2 | `PA8` / `PA9`（TIMA0_C0/C1） | 2026-07-26 |
| 电机 M2 IN1/IN2 | `PB12` / `PB13`（TIMA0_C2/C3） | 2026-07-26 |
| **电流采样（⚠ 实测映射，与 syscfg 原假设相反）** | **M1 = `PA26`(ADC0 MEM1)、M2 = `PA27`(ADC0 MEM0)** | 2026-07-26 |
| 编码器 enc1(M1) A/B | `PA7` / `PB19` | 2026-07-26 |
| 编码器 enc2(M2) A/B | `PB20` / `PB21` | 2026-07-26 |
| LCD GC9A01（SPI1） | SCK`PB9` MOSI`PB8` RES`PB10` DC`PB11` CS`PB14` BLK`PB26` | 2026-07-26 |
| IMU ICM42688（共享 SPI1） | +MISO`PB7`、软片选 CS`PB6`、INT`PA29`(未接)。**真机验活 ✅**：`g` → `WHOAMI=71 OK(0x47)`、静止陀螺 ≤0.25dps、**加速度模长 0.995g**(定标正确)、温度 29.5℃。⚠️ **yaw 轴向仍未定**（`attitude.h` 的"Z 朝上"假设未证）。⚠️**别拿斜放时的读数定轴**——已实证：两次 `g` 给出两个不同主导轴（`a808122` 那次 274/**858**/423 指向 Y；07-27 复测 372/614/**696** 指向 Z），同一块板推出相反答案。**复测 \|a\|=999.9mg**(定标再确认)、静止陀螺 ≤0.24dps。定轴用 `car/imu_axis.ps1 -Step L1/L2/L3`（判据：某轴 ≥900mg 且其余 ≤250mg，不满足判 INCONCLUSIVE） | 2026-07-27 |
| 调试串口 UART0 | TX`PA10` / RX`PA11` @115200（**交叉接 DAP 的 RX/TX + 共地**） | 2026-07-26 |
| SWD | SWCLK`PA20` / SWDIO`PA19`（外置 CMSIS-DAP）。**⚠️ 纠正（2026-07-27 实测）：原记"只接 DIO/CLK/GND"是错的——`nRESET` 也接了且可用**，但必须 `reset_config srst_only srst_push_pull`（默认的 open-drain 拉不低，表现为"复位无效"）。⇒ 软件驱动复位/救砖/冷启动全部可行，见 §D2 第 3 条 | 2026-07-27 |
| **电磁铁驱动（v1.3 第三颗 DRV8231）** | `IN1=PB0`(TIMA1_C0)、**IN2 硬件接 GND ⇒ 单向**、12V 直供、电流采样 `PA24`(ADC0 **MEM2**=A0_3)。**硬件在板 ✅ / 固件已配置 ✅（2026-07-27，`编译级 + syscfg 生成 ok`；PB0 从未输出过 ⇒ `待真机验证`）**：`car.syscfg` 已实配 `PWM_MAG`=TIMA1_C0/PB0(20kHz，上电占空 0) + ADC `MEM2=CHAN_3/adcPin3=PA24`(`endAdd=2`)；驱动 `magnet.h/.c`（吸合满占空 → `CFG_MAG_PULL_MS` 后自动降到保持占空 → `CFG_MAG_MAX_ON_MS` 强制断电防闷烧）；命令 `E0/E1/E<2..100>`；bring-up 脚本 `car/magnet_test.ps1`。⚠️ **上板前必须先看线圈标称电压**：12V 直供且无电流环 ⇒ 唯一限流手段是 `CFG_MAG_PWM_CAP`，6V/9V 线圈必须先按 (额定/12)×100 调低再烧。⚠️ **电流读数判不出"吸住了没"**（电磁铁稳态电流≈V/R，与负载无关；变的是电感）⇒ 它只能证"通电了/没断线"，"吸住了没"要用清单里的**光电传感器**。⚠️ **ADC 序列已从 2 路变 3 路** ⇒ `motor.c` 的 `ADC_SEQ_N` 与等待周期已同步加长，**再加通道必须再改一次**，否则读到陈旧值且无任何报错。⚠️ **四驱改版定版把这颗标了 DNP** 并把 `PA24` 挪给 M4 电流（理由"车用不上电磁铁"）——**若考电磁吸取/搬运题该决策须回滚**；现板(2WD v1.3)不受影响 | 2026-07-27 |
| **无线遥测 ESP-01S #2（UART3）** | MCU→ESP.RXD = `PB2`(**UART3_TX**)、ESP.TXD→MCU = `PB3`(**UART3_RX**)。复用号按手册 ZHCSSC4A p12/p13 核实，且=载板 §10.1 定版分配；生成码已坐实 `ESP_UART_INST=UART3` / `PINCM15=PB2_UART3_TX` / `PINCM16=PB3_UART3_RX`。**硬件已飞线 ✅ / 固件已烧入 / UART3 双向真机验证 ✅（2026-07-27）** —— **TX 判据** = sink 切换二值实验：`l2`(只发无线) ⇒ 有线口 3s **0 行**、`l1` ⇒ 恢复 30 行/3s（且 `l2` 期间仍能发进命令 ⇒ 有线 RX 与 sink 无关）。**RX 判据** = 遥测 `rej` 随发送增长(0→1192)、`l1` 停发后立即冻结 ⇒ `ESP.TXD→PB3` 真接上了（停发即冻结同时排除了"ESP 供电不足在复位循环"）。双发边际代价 **实测 +6.7ms/行**（`f5` 下 115/s → 65/s，与 76B×10bit÷115200=6.6ms 吻合）⇒ **`f20` 以上整定要用 `l1`/`l2` 只留一口**。**✅ 无线端到端真机通了（2026-07-27）**：原版 `read_serial.ps1`/`uart_send.ps1` 打 PC 端 COM 口，**零改动**收遥测 + 下发命令；`f100` 下 **10s/100 行 = 精确 10Hz 零丢行**。⚠️ **角色与原设计相反**：现在 **车端=STA / PC端=AP**（因为已配好 `SAVETRANSLINK` 的那块拔不动、只能留在 PC 侧）⇒ **车端依赖 PC 端 AP 先在线**。定版配置与取舍见 `调试日志.md` `M-无线打通` 条。**✅ 工况测试（2026-07-27，悬空台架）**：遥测率扫到 **50Hz 全 0% 丢包**（⇒ 无线整定可行，`tune_step.ps1` 原版零改动已跑完一轮）· **电机三档（v150 同向 / r250 原地转 / 静止）全 0% 丢包、断档 0、`rej` 全程 0** ⇒ EMI 与 ESP 供电均无问题。⚠️ **悬空负载轻 ⇒ EMI/电流冲击比落地轻，落地需复测**；⚠️ **`f50` 起固件端节拍开始漏拍**（dt max 95ms），要精确 dt 就读遥测里的 `t<ms>`、别假设设定周期=实际周期。数据表见 `调试日志.md` `M-无线工况测试`。🔴 **供电现状（2026-07-27 真机坐实）：车端 ESP 的 `VCC` 挂在 DAP 的 USB 3.3V 上、不是电池 ⇒ 拔掉 DAP 它就死，现在物理上无法脱缆。** 判据：拔 DAP → MCU 照常跑而 ESP 死（PC 端监听 20s 零 station 事件）／插回 → MCU 未重启只有 ESP 复活。**处置 = 把 `VCC` 挪到主板那条电池供的 3.3V（载板 §2.2 本就设计给 `2×ESP+ICM+DRV VREF`）+ 就近 100µF/100nF**（发射峰值 ~300mA，§2.3 警告过 AMS1117 余量紧）。⚠️ 此前所有无线测试（端到端/EMI/soak/整定）**DAP 全程插着**，故该依赖被掩盖 ⇒ **脱缆前必须先拔掉调试器跑一次**（坑库同名条）。
⚠️ 模块若还插着 USB-TTL，**必须拔掉 USB-TTL 的 TX 线**（`ESP.RXD` 会被两个输出驱动）。⚠️ **ESP 不该接调试口** —— 旧文档写 `PA10/PA11` 才引出"必须拔 DAP 串口线/PA11 打架"，按定版表不存在；走 UART3 ⇒ 有线 COM 与无线桥可同时在线。ESP #1 预留 `PB15/PB16`(UART2，`待核`) | 2026-07-27 |
| 板载用户 LED | `PB22` | 2026-07-26 |
| **选脚黑名单（核心板未引出）** | `PA3/PA4/PA5/PA6`(晶振)、`PA19/PA20`(SWD) | 2026-07-26 |

---

## C. 已真机标定/达标的参数（**改值必同步 `config.h` 并重烧验证**）

| 参数 | 值 | 怎么来的 | commit | 校验于 |
|---|---|---|---|---|
| `ENC_CPR`（输出轴每圈计数，4x） | ~~800~~ → **954.75**（✅ **已烧入并在跑**，2026-07-27 18:0x） | 800 是**旧电机 MG310P20** 的（10线×4×20:1）。换 JGA27-310R-74 后手转实测两轮各 10 圈：**左 9525→952.5 · 右 9570→957.0，均值 954.75**（两侧差 0.47%），比旧值高 **19.3%**；结构上对应 **12线×4×20:1=960**，实测为其 99.2~99.7%（差值=人工停位误差）。同次校验：只转一侧时另一侧增量 **0**（映射正确、无串扰）、静止漂移 **0**、两轮前进方向同为正（无 A/B 反接）。**只影响 RPM 标尺**；`ENC_COUNTS_PER_MM` 直接测 counts→mm、**不经过本值**。⚠️ 片上仍是 800 ⇒ 现在遥测的 rpm 偏低约 16%。复验 `car/enc_delta.ps1 -Base1 <c1> -Base2 <c2> -Turns N -Wheel L\|R`（读两次求差，不必卡时机） | 待重烧 | 2026-07-27 |
| 速度环 Kp/Ki | **0.15 / 0.02** | 修完时基 5x bug 后重整定；std~3%，松手过冲 276→207（0.20 会振） | `4bbaae5` | 2026-07-26 |
| 位置环 Kp/Kd（纯 PD） | **0.20 / 0.05** | mode3 级联，阶跃 800counts 两轮到位 ±9counts | `3a4c6f5` | 2026-07-26 |
| 位置环死区前馈 / 到位容差 | **w=12% / e=15counts** | 前馈**按位置误差方向**叠（按速度输出符号会末端震荡）；两轮到位 ±6~15 | `7887366` | 2026-07-26 |
| 控制主时基 | **SysTick 5kHz**（编码器采样同 ISR） | 旧版"数主循环拍×假设1ms"被 LCD 拖慢 → RPM 虚高 5x | `4bbaae5` | 2026-07-26 |
| PWM 上限 / 周期 | **60% / 计数 1600（≈20kHz）** | 7.4V 电机跑 12V 母线，封顶保护 | — | 2026-07-26 |
| 电机死区 / 线性增益 | **≈10% PWM / ≈34 单位每 %** | 开环标定 | `8dd7f86` | 2026-07-26 |
| 电流定标 | `I(mA) ≈ raw*3300/4387`；16 次平均 | 1575µA/A × 680Ω 纸面值，**残余噪声 ±40~100mA** | `e95631d` | 2026-07-26 |
| **counts → mm（里程 `ENC_COUNTS_PER_MM`）** | **5.109**（✅ 已回填 `config.h` **且已烧入**；指纹 `c0` → `counts/mm*100=510`） | **两趟独立实测（不同距离、不同固件路径，是真正的交叉验证）**：① `m7 v80` 2s 无航向修正 → **2044 counts / 卷尺 394mm = 5.1878** ② `n300` 距离闭环+陀螺航向 → **1609.5 counts / 卷尺 315mm = 5.1095**。**两者只差 1.5%**，而卷尺读数本身 ±5mm=±1.6% ⇒ 在不确定度内一致。**合并 = 总计数 3653.5 ÷ 总距离 709mm = 5.153**。自洽校验：等效轮径 58.6mm（与常见 58/60mm 轮吻合）。⚠️ **别再打磨这个常数** —— 见下方"`n<mm>` 定位误差预算"行：命令 300 实走 315，其中 **16mm 是刹车滑行、只有 2mm 是标定误差**。⚠️ 换轮/换胎/换地面必重标。**为 0 时固件明确拒绝走 N mm**（回 `FAIL=NO_CAL`），绝不"当 1.0 用" | `734e1ab` | 2026-07-27 |
| **`n<mm>` 定位误差预算 + `CFG_NAV_COAST_MM` 滑行补偿** | 旧：命令 300 → 实走 **310~315mm（+5%）**。已加补偿 `CFG_NAV_COAST_MM=15`（已烧入）；⬜ **补偿效果待卷尺定论** | **误差拆解（这是"该往哪使劲"的依据）**：固件判到位时 believed 291~294mm，按真值换算实际已走 295~299mm ⇒ **标定只贡献 1~4mm**；之后**滑行 14.3 / 16.4mm（两趟只差 2mm ⇒ 确定性）** ⇒ **净 +10~15mm 由滑行支配**。⇒ **别再打磨 `ENC_COUNTS_PER_MM`**（最多省 2mm）。**改动**：`nav.c` 到位判据由 `fabsf(rem)<=tol` 改为"沿行进方向剩余 ≤ `max(coast_mm, tol_mm)` 就收油"；用带方向的 `rem_dir` 而非 `fabs` ⇒ 冲过头立刻结束、不再掉头往回追（追回去还会再滑一次）。`coast_mm=0` 退化为旧行为。**PC 单测 ALL PASS**（新增 `test_straight_coast`：前进/倒车 × coast=0/15 四组合断言"停止点 = 目标 − max(coast,tol)"）。**真机 #3 已见效**：收油点从 296~299mm 前移到 **289mm**（`done_mm=289 err_mm=10`）。⚠️ 滑行随**末端速度 `CFG_NAV_V_MIN` 与地面**变，换地面/换电池后要重测 | — | 2026-07-27 |
| **counts → deg（转角 `ENC_COUNTS_PER_DEG`，⚠ 仅编码器兜底路径用）** | **⬜ 未标定（=0.0f）** | (右轮增量−左轮增量)/度。**标法**：`nav_test.ps1 -CalTurn 360`（开环原地转，给 `-MeasDeg` 量角器读数才算真值，不给则脚本判 INCONCLUSIVE）。运行时下发 `q<x100>`。⚠️ 它的前提是"轮子不打滑"，而原地转最容易打滑 ⇒ **永远次选**，`k` 标定过(heading_ok)时导航层一定用陀螺 | — | — |
| **偏航参考方向 = 重力投影（主路径，2026-07-27 改）** | **⬜ 未真机验**（PC 单测 PASS） | 命令 `k`（静止 2s、**车处于真实行驶姿态**）一次产出①陀螺零偏②**天顶单位向量 `up`**；每拍 `wz = dot(gyro-bias, up)`。**⇒ 不要求 IMU 装正、不要求车放平、重标只需按一次 `k`**。理由与量化见 `attitude.h` / `config.h §6.5`。真机判据：`k` 后手转 90° → `Y:` ≈ ±900(0.1°) | — | — |
| **`CFG_YAW_AXIS` / `CFG_YAW_SIGN`**（已降为**回落路径**） | 默认 2 / +1 | `CFG_YAW_AXIS` 只在**从未 `k` 过或 `k` 时 \|a\| 异常**（`g_up_valid=0`）时才生效 ⇒ **正常流程不必再定它**，`imu_axis.ps1 -Step L1`（平放挑轴）由必需降为可选诊断。`CFG_YAW_SIGN` 仍有效（投影法也乘它），仍需 L2 式验证：左转应读正 | — | — |
| **陀螺静态漂移 / `CFG_GYRO_DEADBAND_DPS`** | **保持 0（纯积分）—— 实测结论：不需要死区，需要的是"每次上电做 `k`"** | **真机实测（2026-07-27，`run_log.ps1` 各采 15s，静止桌面，单变量只差有没有 `k`）**：**标定前 yaw 漂 −2.4°/15s ≈ −9.6 °/min、峰值\|wz\| 2.0dps** → **标定后 +0.1°/15s ≈ +0.4 °/min、峰值\|wz\| 1.6dps** ⇒ **零偏标定把漂移压掉约 24 倍**。0.4°/min 意味着跑一趟 20s 只漂 0.13°，已远小于任何转向精度需求 ⇒ **加死区收益≈0、代价是极慢速转向被吃掉**，故不加。⚠️ 由此得出流程铁律：**每次上电必做 `k`（车按行驶姿态放稳、松手）**，这比调任何参数都重要 | — | 2026-07-27 |
| LCD 开机默认页 | **水平仪页**（`CFG_DISP_BOOT_PAGE=1`） | 开机必在 IDLE，此页正好答"IMU 活着吗/车平不平"；发运动命令自动切回计数页；`u0`/`u1` 手动切。**附带：不依赖串口就能看到画面**（串口独占、常被占）。⚠️ **它不是定轴前置**——投影法不要求居中，居中只影响 pitch/roll | — | 2026-07-27 **画面已人眼确认正常** |
| 电池/电机 | **JGA27-310R-74 · 20:1 · 7.4V · 450RPM**（2026-07-27 换装，原 MG310P20 已下车）+ 霍尔编码器（A/B 经处理进 3.3V 脚） | 用户换装 | — | 2026-07-27 |
| ⚠️ **换电机的连带后果（别拿旧值继续算）** | 本表**以下各行凡在 2026-07-26 标定的、都是旧电机的值 ⇒ 全部 `待重标`**：`ENC_CPR` · 速度环 Kp/Ki · 位置环 Kp/Kd · 死区前馈 w · 电机死区/线性增益。**旁证**：新电机在 45%/60% 占空下遥测只报 245~253 rpm，而标称 7.4V/450RPM、60% 占空 ≈7.2V 本该接近 450 ⇒ **怀疑新编码器 ≈400 counts/圈而非 800（差 2 倍）**，重标命令 `car/enc_hand_cal.ps1`（不通电、手转 8 圈，同时验两路编码器可信性） | 遥测实测 | — | 2026-07-27 |

> **达标即锁死**（校赛B 铁律）：任何真机整定出来的值，**当场回填 `config.h` + commit**，别只留在串口窗口里。

---

## D. 环境 / 命令事实

> ⚠️ **本节的"工具装在哪"是机器相关的，不是全仓唯一值。** 备赛已实证 **≥2 台机器/clone 并行**（本地 clone 的 `git log` 里没有归档记的 `c1b44ca..d404822`，SSOT 里也引用了本地不存在的 `a808122`）。所以：**唯一可靠入口 = `car/一键编译烧录.bat`**（自动探测五样工具）；文档里的绝对路径只能当"某台机器当时的快照"读，别照抄去跑命令。

| 事实 | 值 | 校验于 |
|---|---|---|
| 工程路径 | `workbench/mspm0/car/`（**必须全 ASCII**——GCC 不认中文路径） | 2026-07-26 |
| 编译（**推荐·唯一入口**） | 跑 `car/一键编译烧录.bat` —— 自动探测 SDK / ARM gcc / make / SysConfig / openocd 五样，并在 **make 命令行**覆盖（makefile 用 `?=`，命令行胜出），**换机零改动**；缺哪样会逐项报 MISSING | 2026-07-27 **编译已验** |
| ⚠️ **下面两行别按"本机"字样理解 —— 两台开发机布局完全不同，"本机"取决于谁在读** | **判定你这台的唯一办法 = 跑 `car\env_check.ps1`**（或看 `_tools.ps1` 解析结果）。2026-07-27 实测：**这台会话机没有 D 盘**（`_tools.ps1` 会对 D 盘候选根喷一串 Join-Path 错误但仍返回正确的 C 盘路径）⇒ 见"机器② C 盘布局"那行 | 2026-07-27 **实测** |
| **机器①「D 盘布局」（= 队友 zhy-ss 那台，不是这台）** | `D:\toolchains\` 下：`arm-gnu-12.2`(ARM GNU **12.2.MPACBTI-Rel1**) · `mspm0-sdk`(SDK **2.09.00.00**, GitHub tag `mspm0_sdk_2_09_00_01`) · `sysconfig-1.23.1`(**1.23.1.4034**) · `xpack-openocd-0.12.0-7` · `build-tools\bin\make.exe`(GNU make **4.4.1**)。装 D 盘因为那台 C 盘只剩 23GB。安装包留在 `D:\toolchains\_dl\`（赛场断网可离线重装，勿删） | 2026-07-27 **编译已验（那台）** |
| 为什么 `build-tools` 里只留 `make.exe` | 同目录的 `sh.exe`/busybox 已**故意删掉**：一旦 PATH 上有 `sh.exe`，make 会把 `SHELL` 切成它 → SDK `imports.mak` 走 Linux 分支（`rm -f`）且 SysConfig 那个 `.bat` 调用会出问题。没有 sh.exe ⇒ `SHELL=cmd.exe` ⇒ 与历史可用环境一致 | 2026-07-27 |
| SDK 的 `imports.mak` 从哪来 | GitHub 版 SDK 只带 `imports.mak.windows`（含 `##GCC_ARM_VER##` 占位符）。bat 会自动 `copy` 成 `imports.mak`；占位符无害，因为三个工具路径都被 make 命令行覆盖 | 2026-07-27 **编译已验** |
| ⬜ **待验：跨机器产物一致性** | 本机 SDK 2.09.00.00 + SysConfig 1.23.1.4034；另一台是 SDK main 快照 + SysConfig 1.28.0 → **同一份 `car.syscfg` 在两台机生成的 `ti_msp_dl_config.c` 是否逐字节一致，未验证**（`gcc/` 是构建产物、被 gitignore，不随 clone 回来，无法直接比对）。两台机产物有差异时优先信"真机跑过"的那一版 | — |
| **机器②「C 盘布局」= 2026-07-27 这台会话机（`_tools.ps1` 实测解析值）** | ARM gcc root `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1` · make = **WinLibs `mingw64\bin\mingw32-make.exe`**（注意是 `mingw32-make.exe`，没有 `make.exe`）· SDK `C:\ti\mspm0-sdk` · SysConfig `C:\ti\sysconfig_1.28.0\sysconfig_cli.bat` · 另有 `C:\ti\ccs2100`。**这台没有 D 盘。** 编译已验：`make exit=0`、SysConfig 生成 unchanged、`car.obj`/`car.out` 重链成功 | 2026-07-27 **编译已验（这台）** |
| **只编译不烧录** | **`car/build.ps1`**（`734e1ab` 新增，**唯一编译-only 入口**；`-Touch` 强制重编 `car.obj`）。为什么需要它：`一键编译烧录.bat` 是 build+flash **一体、没有停在编译的开关** ⇒ 每次"验一下还能不能编"都要烧一次板，与禁忌 2（别频繁烧录）直接冲突。脚本内置三件今天亲自踩过的事：① 过滤 SysConfig 那十几行噪声、只留 `Building/linking/error/warning` ② **按 `car.out` 有没有重链判"到底编没编"**（尾部日志会被 SysConfig 刷满，我据此误判过一次 `config.h` 没生效）③ 检出"源文件比 `car.out` 新"的静默失效。输出 `text+data` 作为 `wrote N bytes` 对账值；三态退出码。**实测 PASS**：`-Touch` → `Building car.obj` + `linking car.out`、exit 0、text 42144+data 168=**42312** | 2026-07-27 **PC 已验** |
| 烧录（推荐） | `car/flash.ps1` —— **2026-07-27 重写为单会话 + 全自动，真机 PASS**。**命令顺序有讲究，别改**：`init` → **`reset halt`（必须用目标默认的 sysresetreq）** → `flash write_image erase` → `verify_image` → **然后才切 `reset_config srst_only srst_push_pull`** → `reset run`。⚠️ **srst_only 不能全局挂着**——官方 cfg 明写它会让 `reset halt` 失效（debugss 受 nRST 影响），实测 `TARGET: Not halted` 且一个字节都没写进去；这也正是官方 `mspm0_board_reset` 要临时保存/恢复 reset_config 的原因。⚠️ **必须先 halt 再擦**，app 在跑时擦除报 `Please halt target for erasing flash`；且**要用 `reset halt` 而非裸 `halt`**（裸 halt 一个运行中的 app 出现 `target was in unknown state`，那一次写完出现 **16 字节静默不一致**）。**不再需要手按 RST**，也**不再需要两段式**（两段式当初是为绕开 `program` 的 CRC helper 假失败；`flash write_image erase`+`verify_image` 不用那个 helper，所以一个会话就够 ⇒ 会话更少=砖化机会更少）。**成功判据 = `wrote N bytes` + `verified N bytes`，且 N == `arm-none-eabi-size` 的 text+data**（脚本自己算并比对，别记死数字）。写入约 **75~90s @500kHz，绝不可打断**。⚠️ **第二种假失败（2026-07-27 实测，比 CRC 超时那种更坑）：`verify_image` 回落的 host-side 字节比对会读到「擦除前的陈旧数据」，于是报 `diff N address 0x…` + `RESULT: FAIL`，而 flash 内容其实完全正确。** 本役 10 个坏字节全落在**两个 8 字节对齐的字**(`0x0`/`0x5800`)，独立只读会话 `mdw` 读回全是新值，且 verify 报的那串"旧字节"实际位于 `0x5a68`(新镜像里 Reset_Handler 搬去的地址)⇒ 陈旧读。**⇒ 看到 `diff` 别直接重烧**（会白烧 87s + 多担一次 brick 风险）：先 `openocd … -c init -c "mdw <addr> 4"` 只读复核那几个地址；再看**功能指纹**（本役 `?` 回 `sinks=3 rej=0`）。**两条判读启发**：坏字节若都 8 字节对齐且只有两三处 ⇒ 不是 SWD 信号质量，别降速；"擦过的扇区不可能读回旧值"这个矛盾本身就是"读回不可信"的信号 | 2026-07-27 **真机已验** |
| **⚠ openocd 每次 `init` 都会复位芯片** | 官方 `target/ti_mspm0.cfg` 把 `_mspm0_enable_low_power_mode` 挂在 `examine-start` 上，而它会写 `PWR_AP::SPREC <= SYSRST=1` ⇒ **每建立一次 openocd 连接，app 就重启一次**（实测：新会话第一次读 `g_st` 恒为 0）。**推论**：① 想用 SWD 写 RAM 改运行时变量（如切 LCD 页）**不可行**——写完就被复位清掉，运行时状态只能走串口命令；② 读全局变量只能读"刚启动后"的值，别拿它判长期行为 | 2026-07-27 **真机已验** |
| 无头 SWD 调试 | `car/dbg.ps1 probe|registers|run-to-symbol`（包装 `.kiro/skills/mspm0-ccs/scripts/openocd_debug.py`）— **halt 会让 PWM 冻在当前占空，先发 `z` 停机** | 2026-07-27 `待真机` |
| 救砖 | `car/unbrick_flash.ps1`（**解锁+烧录必须同一 openocd 会话**——factory_reset 会擦空 flash，空 flash 会重新上锁）。**2026-07-27 起加 `srst_push_pull` ⇒ 全自动、不需要人点 RST**（AI 可自行救砖）。判据 `Factory reset success!` → `wrote N bytes` → `verified N bytes` | 2026-07-27 **真机已验** |
| **⭐ 脚本里的工具路径只在一处解析** | `car/_tools.ps1`（dot-source，导出 `Find-Openocd` / `Find-ArmTool`）。`flash.ps1`/`unbrick.ps1`/`unbrick_flash.ps1`/`dbg.ps1` 已全部改为调它、**不再各写死一份 `C:\ti\...`**。**换机/换盘只改 `_tools.ps1` 的 `$ToolRoots`**。找不到就 `throw`（宁可响亮失败，也不悄悄用 PATH 上的别的 openocd）。教训：这四个脚本原先各写死 `C:\ti\xpack-openocd-0.12.0-7`，在本机（装 D 盘）**一跑即失败**——而暴露时机正是芯片 lockup、`unbrick_flash.ps1` 是唯一救命脚本的时候 | 2026-07-27 **PC 已验**（解析出 D 盘 openocd/gdb，5 脚本语法检查通过） |
| **🔧 工具链路径（⚠️ 本节不再登记绝对路径，它是机器事实不是项目事实）** | **唯一解析点 = [`car/_tools.ps1`](../../workbench/mspm0/car/_tools.ps1)**（导出 `Find-Openocd`/`Find-OpenocdScripts`/`Find-ArmBin`/`Find-ArmTool`/`Find-MakeBin`/`Find-SdkRoot`/`Find-SysConfigCli`）。解析序 = ① 环境变量（`DIANSAI_OPENOCD_ROOT`/`DIANSAI_ARM_BIN`/`DIANSAI_MAKE_BIN`/`MSPM0_SDK_INSTALL_DIR`/`DIANSAI_SYSCONFIG_CLI`）→ ② `car/_tools.local.ps1`（**gitignore，每台机自己的**；模板 `_tools.local.example.ps1` 入库）→ ③ 已知候选根 + glob → ④ **throw 并打出"看了哪些路径 + 三种修法"，绝不偷偷用 PATH 上的同名工具**。**⇒ 想知道你这台机的实际路径，别查文档，跑 `car\env_check.ps1`**（doctor：一张 PASS/FAIL 表 + 版本 + `car.out` 的 text+data；退出码 `0=PASS / 1=FAIL / 2=SKEW`）。**背景**：2026-07-27 实测两台开发机路径完全不同，此前 11 个文件写死 25 处绝对路径 ⇒ 队友机上 `flash`/`unbrick_flash`/`dbg`/`syscfg_check`/`sdk_find`/`一键编译烧录.bat` **全部一跑即挂**（含救砖脚本）。 | 2026-07-27 **PC 已验**（本机 PASS；**队友机 `待验证`**） |
| **⚠️ 跨机版本 SKEW（会导致"同源不同二进制"）** | 本机 SDK **2.11.00.07** + SysConfig **1.28.0** + gcc **12.2.1**；队友机（据其文档）SDK 2.09.00.00 + SysConfig 1.23.1.4034。**SysConfig 是代码生成器、SDK 带 driverlib 源码 ⇒ 同一 commit 两台机可编出不同二进制**。基准值钉在 [`car/toolchain.lock`](../../workbench/mspm0/car/toolchain.lock)，`env_check.ps1` 报 **MATCH / SKEW**（**只报不拦**——赛前硬拦会把队友挡在门外）。**⇒ 现行规矩（流程解，非技术解）：待烧二进制只由一台机产出**（带去赛场那台），另一台只写码 + 跑 `pc_test/`。因此 `wrote N bytes` 判据**是机器相关的**，永远拿当场 `env_check.ps1` / `arm-none-eabi-size` 的输出对账。 | 2026-07-27 **PC 已验** |
| **改完 `.syscfg` 先自检** | `car/syscfg_check.ps1`（静态体检 + 临时目录试生成；**钉死 SDK `imports.mak` 的 `SYSCONFIG_TOOL`，用错版本等于白验**）— 实测 `status=ok`、无 warning。⚠️ 脚本里"本机有两个 1.28.0"是**另一台机**的情况；**本机只有 1.23.1.4034**（`D:\toolchains\sysconfig-1.23.1`），在本机跑前先确认它探到的是这个 | 2026-07-27 **PC 已验（另一台机）** |
| **查官方例程怎么配外设** | `car/sdk_find.ps1 <MODULE> [-Grep kw] [-AllBoards]`（索引本机 SDK 1765 个 `.syscfg` + `.meta/*.syscfg.js` 字段真源）—— 加外设前先查，别凭记忆猜 | 2026-07-27 **PC 已验** |
| ADC 同步定相采样参考例程（电流环前置） | **SDK 根**下 `examples\nortos\LP_MSPM0G3507\driverlib\adc12_triggered_by_timer_event`（多路同步 = `adc12_simultaneous_trigger_event`；整套 = `motor_control_pmsm_sensorless_foc\*`）。SDK 根在本机 = `D:\toolchains\mspm0-sdk`（**别写死盘符，见本节顶部警示**） | 2026-07-27 |
| **查在线资料的链路（AI 侧能力边界，2026-07-27 实测）** | ① **`web_fetch` 不支持 PDF**（只吃 text/html/json），而 TI 手册与应用笔记**全是 PDF**；② **通路是"搜到 URL → PowerShell 下载 → 本机 `.venv` PyMuPDF 提取 → AI 读"**——已端到端实测通（`slvae59.pdf` 1.03MB 下载 + 19 页全文提取，`.venv/Scripts/python.exe` 内 **PyMuPDF 1.28.0**）；③ 下载**必须** `$ProgressPreference='SilentlyContinue'`，否则 `Invoke-WebRequest` 进度条会把输出刷爆（实测撑爆 30000 字符上限、正文被截）；④ **搜索触达**：ti.com / GitHub / 博客园 / stackexchange **可达**；**知乎 / CSDN / B站 / 公众号 / QQ群 基本不可达**（三次检索零命中）⇒ **别把赛中方案建立在"到时能扒到中文社区讨论"上**；⑤ 下载的第三方 PDF 落 `.tmp_pdf/`（已 gitignore）——**版权原因不入库**，只入库要点摘录 + URL | 2026-07-27 **PC 已验** |
| 调试串口 | **COM30** @115200（DAP 的 VCOM；号会随拔插变，先扫端口） | 2026-07-26 |
| **push 被 `Connection was reset` 打回时** | 加两个开关就过：`git -c http.version=HTTP/1.1 -c http.postBuffer=524288000 push origin main`。**2026-07-27 两次实测**：裸 `git push` 连试 2 次都 `Recv failure: Connection was reset`，加上这两个开关一次就过（`96b051a`、`4044c29` 都是这么推上去的）。⚠️ 赛场手机热点会更常撞这个（本仓库实测卡过半小时）⇒ **别把它当偶发，第一次失败就直接上这条**。判据仍是尾行 `xxx..yyy main -> main`（PowerShell 会把 git 的 stderr 显示成红色 error，别被骗停） | 2026-07-27 **实测有效** |
| **ESP-01S 的 COM 口（本机 + 拓展坞）** | **只有插在 PC 上的模块才有 COM 口**；**COM6 = 模块 B（PC 端/STA `192.168.4.2`，CH340K）**。⚠️ **COM30 不是 ESP** —— 它是板载 CMSIS-DAP 的 VCOM = MCU 的 `UART0(PA10/PA11)`；ESP-01S 无 USB，**装在板上的那块（`PB2/PB3`）PC 根本碰不到**，唯一通路是 MCU（固件还没配 UART3）。判别在手模块 = `esp_at.ps1 -Escape -Cmds "AT+CWSAP?;AT+CIPAPMAC?"` —— **看 SSID/MAC，不能只看 CWMODE+IP**（2026-07-27 踩过：**出厂模块同样是 `CWMODE=2` + AP IP `192.168.4.1`**，与已配好的 A 完全撞脸 ⇒ 旧判别法分不出）。**A** = SSID `DIANSAI_CAR` / AP MAC `ea:db:84:b3:13:d0`（存了 `SAVETRANSLINK`，**必须装车端**）· **B** = `CWMODE=1` + STA IP `192.168.4.2` / MAC `8c:aa:b5:cf:e8:f6`（装 PC 端，每次会话跑 `esp_pc_up.ps1`）· 出厂第三块 = SSID `AI-THINKER_*`。⚠️ **三块模块外观完全相同、无法目视区分**，装之前先用上面这条命令认一遍；起 B = `esp_pc_up.ps1 -Port COM6 -Verify`（含**不持久**的 `AT+SLEEP=0`，不发它抖动差 4 倍）。⚠️ **`esp_link_test.ps1` 两个口都在本机时是自环**，测得再好也不代表车通了（判据：STA 的 `CWJAP?` BSSID 若 == 另一块的 AP MAC 就是自己跟自己）。⚠️ **A 侧 `AT+CWLIF` 空 ≠ 没连上**（B 用静态 IP、不进 DHCP 表），看 A 复位时打的 `+STA_DISCONNECTED:<B的MAC>` | 2026-07-27 **真机已验** |
| 串口发命令 | `car/uart_send.ps1`（**逐字符 + 25ms 间隔**——一次突发写会撑爆 MCU RX FIFO 丢字节） | 2026-07-26 |
| 固件命令集 | `m0..m10` 模式(m6 开环差速/m7 闭环差速/**m8 走N mm/m9 转N度/m10 视觉伺服**) / `t<v>` 目标 / `p,i,d<×1000>` 增益 / `w,e` 位置精定位 / `f<ms>` 遥测周期 / `x,y` DUAL 直驱 / `v,r` 车级线速度·角速度 / **IMU: `g` 验活+定轴提示、`k` 零偏标定(静止2s)、`o` yaw 归零、`a<0|1|2>` 定竖直轴、`s<1|-1>` 定 yaw 符号** / **`h<ms>` 临时改静默超时(0=恢复默认；绕不过硬上限)** / `u0,u1` LCD 切页 / **`l<mask>` 遥测去向(1=有线DAP / 2=无线ESP / 3=双发，开机默认 3；**真机已验**)** / **`b<秒>` AT 桥接**(有线口 ↔ 车载 ESP 原样对接，5~300s、缺省 30，**超时自动退出**、期间不解析命令不打遥测、进入前 `stop_all()`；**唯一能配置车载 ESP 的通道** —— 它的串口只接 MCU、板上没第二个串口座。**真机已验**) / `z` 停(清全部残留指令) / `?` 状态 / **〔2026-07-27 新增·`编译级+PC单测级`，真机零验证〕车级导航: `n<mm>` 走直(带航向保持，负=倒车) · `j<deg>` 原地转(正=左) · `c<x100>` 里程标定 counts/mm · `q<x100>` 转角标定 counts/deg（`c0`/`q0` = 只回读）；m8/m9 下 `p`/`d` 改的是航向/转角增益(住 `nav_t` 里，纯 PD 无 I)，跑完自动打一行 `[nav]` 成绩单，遥测在导航模式下追加 `NAV:<state>,<err_mm>,<err_deg×10>,<peak_hdg×10>`** / **电磁铁: `E0` 放 · `E1` 吸(满占空→自动降额保持) · `E<2..100>` 直接给占空** / **视觉: `V` 看视觉链健康度(ok/bad_csum/bad_form/overflow + 最近帧 + age)；以 `$` 开头的整行不进命令通道、直接喂帧解析器 ⇒ **不用相机也能测 m10**(PC 发 `$V,id,cx,cy,area*HH` 即可，见 `car/vision_test.ps1`)** | 2026-07-27 |
| **命令格式门（挡 ESP boot 日志误触发命令）** | 规则在 [`car/cmd_gate.h`](../../workbench/mspm0/car/cmd_gate.h)（`static inline`、不依赖 HAL）：只收 `<字母>[-]<数字...>`、单字母无参、裸 `?`，可选 `#` 前缀。**PC 单测 `pc_test/test_cmd_gate.c` ALL PASS**（35 正例含 `?`／21 反例／回放 650 字节真实 boot 流：旧逻辑 22 行过、其中 **3 次 `t`**，新逻辑 **0 行过**）。⚠️ 单测抓出两个真漏洞：**`?` 会被纯"字母+数字"规则拒掉**（而它是最常用命令）、**`t-` 会被当 `t0`**（`parse_int("-")=0` ⇒ 静默把目标设成 0）。**已烧入并在跑**（`?` 回 `… sinks=3 rej=0`，这两个字段只在 `CFG_ESP_UART_EN` 下编入 ⇒ 也是**新固件版本指纹**，旧版打不出来）。**`rej` 计数真机已验**（0→1192，来源是出厂态 ESP 把遥测当 AT 命令逐行回 `ERROR` 的**回声**，全被门拒下 ⇒ 门在干活 + RX 链路通）。⬜ **`CMD_MUTE_MS` 上电静默窗仍未单独验**（要等 ESP 配好后真的吐一次 boot 日志） | 2026-07-27 **PC 已验 + 已烧入 + rej 真机已验** |
| 运动超时自停 + 急停（落地安全三件） | 两道闸门：**静默超时**(按模式 `CFG_RUN_MS_*`，任何命令刷新) + **硬上限 `CFG_RUN_MS_HARDCAP`=15s**(进模式起算，**不可绕过**)。触发 → 回 IDLE + 清全部指令 + 打 `RUN TIMEOUT (SILENCE\|HARDCAP)`。值与取舍见 `config.h §7`。**✅ 三项真机验证全过（2026-07-27，悬空台架）**：① **急停 `z`** → `DRVC→IDLE`、`PWM:0,0`、**`D:0,0`**(车级指令残留被清 = 那个"再进一次 m7 会窜出去"的 bug 已死) ② **静默超时** → `RUN TIMEOUT (SILENCE) in DRVC after 6017ms`(配置 6000) ③ **硬上限** → 期间每秒发 `?` 持续刷新闸门 1，车仍在 **15021ms** 被 `HARDCAP` 停下(配置 15000、误差 21ms)、且 SILENCE 未触发 ⇒ **`h<ms>` 确实绕不过它**。**复验命令**：`car/hardcap_test.ps1 -Port <口>`(一次性、三态 PASS/FAIL/INCONCLUSIVE；⚠️ 轮子必须悬空) | 2026-07-27 **真机已验** |
| **⚠ 改 `config.h` 必须重编成功才算数** | `gcc/makefile` 原先**漏了 `car.obj: ../config.h` 依赖** → 改参数后 `make` 报 "Nothing to be done"、**二进制仍是旧值且无任何报错**（"达标即回填"会静默失效）。**已修 + PC 已验**（`touch config.h` → 必须看到 `Building car.obj`）。补编时**只删 `gcc/car.obj`**，绝不 `make clean` | 2026-07-27 **PC 已验** |
| **跑一趟出成绩单** | `car/run_log.ps1 -Port <口> -Seconds N [-Csv x.csv]` —— 采集 → CSV + 一张表：**真丢包率**(按遥测 `#seq` 缺号算，不是"行数对不对得上期望") · 最长断档 · **固件端 dt** avg/std/max · 首行 `#seq @ t=<ms>`(**冷启动时 = "开机到无线通"的时长**) · 时长 · 左右编码器增量与 DIFF/比值 · yaw 净变化与极值 · 峰值 \|wz\|/RPM。三态 `PASS/FAIL/INCONCLUSIVE` + 退出码。**有线口无线口都能用**（都是 COM 口）。落地跑完的测试表可直接由它出 | 2026-07-27 **真机已验** |
| 遥测行尾字段 | `… \| D:<v>,<w> \| Y:<yaw×10 度> W:<偏航角速度×100 dps>`（`CAL` = 零偏标定中）。脚本按 `Y:`/`W:` 正则取值 | 2026-07-27 |
| **定轴/符号为什么是运行时命令** | 试轴要多组合，**靠改 `config.h` 重烧 = 触发"连续快烧"禁忌**（本板已因此 lockup 过一次）。故 `a/s` 在线切换、一次烧录定完，**定完必须回填 `config.h` 并 commit**（否则只活在 RAM 里，断电即失） | 2026-07-27 |

### D2. 四条禁忌（违反过、代价真实）
1. **绝不用 `make clean`** —— 会删掉 SDK 共享 startup 源文件，之后所有工程编译不过。只删本地 `*.obj/*.out`。
2. **绝不连续快烧 / 反复 halt / 中途打断 program** —— 会把 MCU 怼进 double-fault lockup（`Could not find MEM-AP`）。节奏永远是"烧一次 → 看现象 → 改 → 再烧"。
3. ~~**冷启动只认物理 RST**~~ —— **⛔ 本条 2026-07-27 作废，原因是配错了 openocd 而非硬件限制。** 真相：DAP 的 nRESET **是接到板子的**，但 openocd 对本适配器默认 `srst_open_drain`，那个模式下 nRESET 根本没被拉低 ⇒ 每次 `reset` 都静默地什么也没做，于是被误当成"MSPM0 软复位起不来"。加 **`reset_config srst_only srst_push_pull`** 后 `reset run` 能真正冷启动、`mspm0_factory_reset` 也能**无人值守**跑通（实测：`Factory reset success!` → `wrote 28552 bytes` → `verified 28552 bytes`，全程没人碰板子）。**⇒ 烧录与救砖都不再需要手按 RST**；手点只作为 push-pull 也失败时的后备。
4. **别把循环 spawn openocd 的脚本留在后台 running** —— 会持续抢 DAP、把 USB 拖进驱动层死锁（连串口 open 都 hang），只能拔插 USB 复位。

---

## E. 谁引用了这些事实（改值后按此 `git grep` 扫尾 · 非穷举）

- **引脚** → `workbench/mspm0/car/README.md`、`car.syscfg`、`motor.h`/`encoder.h`/`imu.h` 顶部注释、`天猛星主板平台/00_载板接线设计`(§10.1 真值源)、`四驱改版_接线设计.md`
- **控制参数** → `car/config.h`(真值)、`car.c` 注释、`调试日志.md`、`2026省赛控制押题_预测/04_备赛启示…`、`CONTINUATION_GUIDE.md`
- **烧录/串口命令** → `天猛星主板平台/编译烧录操作手册.md`、`car/README.md`、`car/*.ps1`、`AI调试工具链_借鉴mspm0-skill.md`
- ⚠️ **已知过时副本**：`car/README.md` 的电流通道仍写"MEM0=PA27(M1)"（**旧假设**，实测相反，见 §B）、`encoder.h` 顶部仍留"须 1k/2k 分压"字样（**已作废**，开集输出正解是上拉，见坑库）。**下次碰到这两个文件时顺手改掉。**

---

## 维护日志
- **2026-07-26**：创建。对标桌面 `4.8`（RideWind）的 `project-facts-ssot.md`，把引脚/已标定参数/环境命令/禁忌收敛成单一真值源，反制"抄多处→改一处漏一堆→AI 用旧值分析出错"。同时收录两处已知过时副本待清。
