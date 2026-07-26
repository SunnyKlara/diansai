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
| SWD | SWCLK`PA20` / SWDIO`PA19`（外置 CMSIS-DAP，只接 DIO/CLK/GND） | 2026-07-26 |
| 板载用户 LED | `PB22` | 2026-07-26 |
| **选脚黑名单（核心板未引出）** | `PA3/PA4/PA5/PA6`(晶振)、`PA19/PA20`(SWD) | 2026-07-26 |

---

## C. 已真机标定/达标的参数（**改值必同步 `config.h` 并重烧验证**）

| 参数 | 值 | 怎么来的 | commit | 校验于 |
|---|---|---|---|---|
| `ENC_CPR`（输出轴每圈计数，4x） | **800** | 手转 8 圈增量 6402÷8（原估 899 偏高 12%） | `7887366` | 2026-07-26 |
| 速度环 Kp/Ki | **0.15 / 0.02** | 修完时基 5x bug 后重整定；std~3%，松手过冲 276→207（0.20 会振） | `4bbaae5` | 2026-07-26 |
| 位置环 Kp/Kd（纯 PD） | **0.20 / 0.05** | mode3 级联，阶跃 800counts 两轮到位 ±9counts | `3a4c6f5` | 2026-07-26 |
| 位置环死区前馈 / 到位容差 | **w=12% / e=15counts** | 前馈**按位置误差方向**叠（按速度输出符号会末端震荡）；两轮到位 ±6~15 | `7887366` | 2026-07-26 |
| 控制主时基 | **SysTick 5kHz**（编码器采样同 ISR） | 旧版"数主循环拍×假设1ms"被 LCD 拖慢 → RPM 虚高 5x | `4bbaae5` | 2026-07-26 |
| PWM 上限 / 周期 | **60% / 计数 1600（≈20kHz）** | 7.4V 电机跑 12V 母线，封顶保护 | — | 2026-07-26 |
| 电机死区 / 线性增益 | **≈10% PWM / ≈34 单位每 %** | 开环标定 | `8dd7f86` | 2026-07-26 |
| 电流定标 | `I(mA) ≈ raw*3300/4387`；16 次平均 | 1575µA/A × 680Ω 纸面值，**残余噪声 ±40~100mA** | `e95631d` | 2026-07-26 |
| **counts → mm（里程）** | **⬜ 未标定** | 落地走一段用卷尺量实距反算（含滑移），做完回填 `config.h` | — | — |
| **`CFG_YAW_AXIS` / `CFG_YAW_SIGN`** | **⬜ 未定轴**（默认 2 / +1 只是占位） | 板上跑 `imu_axis.ps1 -Step L1`(平放看哪轴≈±1000mg) → `-Step L2`(左转看符号)；算法侧已 PC 单测（含"选错轴 → yaw 恒为 0"的失败模式断言） | — | — |
| **陀螺静态漂移 / `CFG_GYRO_DEADBAND_DPS`** | **⬜ 未实测**（死区先给 0=纯积分） | `imu_axis.ps1 -Step L3 -Sec 60` 出 °/min，**先测再决定要不要死区**（否则测不出死区有没有必要） | — | — |
| 电池/电机 | MG310P20 **7.4V** + 5V 霍尔编码器（A/B 经处理进 3.3V 脚） | — | — | 2026-07-26 |

> **达标即锁死**（校赛B 铁律）：任何真机整定出来的值，**当场回填 `config.h` + commit**，别只留在串口窗口里。

---

## D. 环境 / 命令事实

> ⚠️ **本节的"工具装在哪"是机器相关的，不是全仓唯一值。** 备赛已实证 **≥2 台机器/clone 并行**（本地 clone 的 `git log` 里没有归档记的 `c1b44ca..d404822`，SSOT 里也引用了本地不存在的 `a808122`）。所以：**唯一可靠入口 = `car/一键编译烧录.bat`**（自动探测五样工具）；文档里的绝对路径只能当"某台机器当时的快照"读，别照抄去跑命令。

| 事实 | 值 | 校验于 |
|---|---|---|
| 工程路径 | `workbench/mspm0/car/`（**必须全 ASCII**——GCC 不认中文路径） | 2026-07-26 |
| 编译（**推荐·唯一入口**） | 跑 `car/一键编译烧录.bat` —— 自动探测 SDK / ARM gcc / make / SysConfig / openocd 五样，并在 **make 命令行**覆盖（makefile 用 `?=`，命令行胜出），**换机零改动**；缺哪样会逐项报 MISSING | 2026-07-27 **编译已验** |
| **本机工具链实际位置（全在 D 盘）** | `D:\toolchains\` 下：`arm-gnu-12.2`(ARM GNU **12.2.MPACBTI-Rel1**) · `mspm0-sdk`(SDK **2.09.00.00**, GitHub tag `mspm0_sdk_2_09_00_01`) · `sysconfig-1.23.1`(**1.23.1.4034**) · `xpack-openocd-0.12.0-7` · `build-tools\bin\make.exe`(GNU make **4.4.1**)。**装 D 盘因为 C 盘只剩 23GB**。安装包留在 `D:\toolchains\_dl\`（赛场断网可离线重装，勿删） | 2026-07-27 **编译已验** |
| 为什么 `build-tools` 里只留 `make.exe` | 同目录的 `sh.exe`/busybox 已**故意删掉**：一旦 PATH 上有 `sh.exe`，make 会把 `SHELL` 切成它 → SDK `imports.mak` 走 Linux 分支（`rm -f`）且 SysConfig 那个 `.bat` 调用会出问题。没有 sh.exe ⇒ `SHELL=cmd.exe` ⇒ 与历史可用环境一致 | 2026-07-27 |
| SDK 的 `imports.mak` 从哪来 | GitHub 版 SDK 只带 `imports.mak.windows`（含 `##GCC_ARM_VER##` 占位符）。bat 会自动 `copy` 成 `imports.mak`；占位符无害，因为三个工具路径都被 make 命令行覆盖 | 2026-07-27 **编译已验** |
| ⬜ **待验：跨机器产物一致性** | 本机 SDK 2.09.00.00 + SysConfig 1.23.1.4034；另一台是 SDK main 快照 + SysConfig 1.28.0 → **同一份 `car.syscfg` 在两台机生成的 `ti_msp_dl_config.c` 是否逐字节一致，未验证**（`gcc/` 是构建产物、被 gitignore，不随 clone 回来，无法直接比对）。两台机产物有差异时优先信"真机跑过"的那一版 | — |
| 另一台机器的 C 盘布局（**仅供对照·本机没有**） | `C:\Program Files (x86)\Arm GNU Toolchain...` + WinLibs `mingw64\bin` + `C:\ti\mspm0-sdk` + `C:\ti\sysconfig_1.28.0` + `C:\ti\ccs2100`。本机重装后这些**全部不存在** | 2026-07-27 |
| 烧录（推荐） | `car/flash.ps1`（**2026-07-27 改两段式**：① `program car.out exit` 只写不校验；② 另起只读会话 `init/halt/verify_image` 做字节比对）。openocd `xpack-openocd-0.12.0-7` + `interface/cmsis-dap.cfg` + `target/ti_mspm0.cfg`，**adapter speed 500**。**成功判据 = `** Programming Finished **` + `verified NNNNN bytes`**；看到裸 `Verify Failed` 先证伪、**别重烧**（见坑库同名条） | 2026-07-27 |
| 无头 SWD 调试 | `car/dbg.ps1 probe|registers|run-to-symbol`（包装 `.kiro/skills/mspm0-ccs/scripts/openocd_debug.py`）— **halt 会让 PWM 冻在当前占空，先发 `z` 停机** | 2026-07-27 `待真机` |
| 救砖 | `car/unbrick_flash.ps1`（**解锁+烧录必须同一 openocd 会话**，响铃时停点 RST） | 2026-07-26 |
| **⭐ 脚本里的工具路径只在一处解析** | `car/_tools.ps1`（dot-source，导出 `Find-Openocd` / `Find-ArmTool`）。`flash.ps1`/`unbrick.ps1`/`unbrick_flash.ps1`/`dbg.ps1` 已全部改为调它、**不再各写死一份 `C:\ti\...`**。**换机/换盘只改 `_tools.ps1` 的 `$ToolRoots`**。找不到就 `throw`（宁可响亮失败，也不悄悄用 PATH 上的别的 openocd）。教训：这四个脚本原先各写死 `C:\ti\xpack-openocd-0.12.0-7`，在本机（装 D 盘）**一跑即失败**——而暴露时机正是芯片 lockup、`unbrick_flash.ps1` 是唯一救命脚本的时候 | 2026-07-27 **PC 已验**（解析出 D 盘 openocd/gdb，5 脚本语法检查通过） |
| **改完 `.syscfg` 先自检** | `car/syscfg_check.ps1`（静态体检 + 临时目录试生成；**钉死 SDK `imports.mak` 的 `SYSCONFIG_TOOL`，用错版本等于白验**）— 实测 `status=ok`、无 warning。⚠️ 脚本里"本机有两个 1.28.0"是**另一台机**的情况；**本机只有 1.23.1.4034**（`D:\toolchains\sysconfig-1.23.1`），在本机跑前先确认它探到的是这个 | 2026-07-27 **PC 已验（另一台机）** |
| **查官方例程怎么配外设** | `car/sdk_find.ps1 <MODULE> [-Grep kw] [-AllBoards]`（索引本机 SDK 1765 个 `.syscfg` + `.meta/*.syscfg.js` 字段真源）—— 加外设前先查，别凭记忆猜 | 2026-07-27 **PC 已验** |
| ADC 同步定相采样参考例程（电流环前置） | **SDK 根**下 `examples\nortos\LP_MSPM0G3507\driverlib\adc12_triggered_by_timer_event`（多路同步 = `adc12_simultaneous_trigger_event`；整套 = `motor_control_pmsm_sensorless_foc\*`）。SDK 根在本机 = `D:\toolchains\mspm0-sdk`（**别写死盘符，见本节顶部警示**） | 2026-07-27 |
| 调试串口 | **COM30** @115200（DAP 的 VCOM；号会随拔插变，先扫端口） | 2026-07-26 |
| 串口发命令 | `car/uart_send.ps1`（**逐字符 + 25ms 间隔**——一次突发写会撑爆 MCU RX FIFO 丢字节） | 2026-07-26 |
| 固件命令集 | `m0..m7` 模式(m6 开环差速/m7 闭环差速) / `t<v>` 目标 / `p,i,d<×1000>` 增益 / `w,e` 位置精定位 / `f<ms>` 遥测周期 / `x,y` DUAL 直驱 / `v,r` 车级线速度·角速度 / **IMU: `g` 验活+定轴提示、`k` 零偏标定(静止2s)、`o` yaw 归零、`a<0|1|2>` 定竖直轴、`s<1|-1>` 定 yaw 符号** / `z` 停 / `?` 状态 | 2026-07-27 |
| 遥测行尾字段 | `… \| D:<v>,<w> \| Y:<yaw×10 度> W:<偏航角速度×100 dps>`（`CAL` = 零偏标定中）。脚本按 `Y:`/`W:` 正则取值 | 2026-07-27 |
| **定轴/符号为什么是运行时命令** | 试轴要多组合，**靠改 `config.h` 重烧 = 触发"连续快烧"禁忌**（本板已因此 lockup 过一次）。故 `a/s` 在线切换、一次烧录定完，**定完必须回填 `config.h` 并 commit**（否则只活在 RAM 里，断电即失） | 2026-07-27 |

### D2. 四条禁忌（违反过、代价真实）
1. **绝不用 `make clean`** —— 会删掉 SDK 共享 startup 源文件，之后所有工程编译不过。只删本地 `*.obj/*.out`。
2. **绝不连续快烧 / 反复 halt / 中途打断 program** —— 会把 MCU 怼进 double-fault lockup（`Could not find MEM-AP`）。节奏永远是"烧一次 → 看现象 → 改 → 再烧"。
3. **冷启动只认物理 RST** —— openocd 软复位起不来（停在 BOOTROM）。烧完手按一次 RST 才算跑新固件。
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
