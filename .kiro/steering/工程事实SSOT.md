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
| IMU ICM42688（共享 SPI1） | +MISO`PB7`、软片选 CS`PB6`、INT`PA29`(未接) | 2026-07-26 `待真机` |
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
| 电池/电机 | MG310P20 **7.4V** + 5V 霍尔编码器（A/B 经处理进 3.3V 脚） | — | — | 2026-07-26 |

> **达标即锁死**（校赛B 铁律）：任何真机整定出来的值，**当场回填 `config.h` + commit**，别只留在串口窗口里。

---

## D. 环境 / 命令事实

| 事实 | 值 | 校验于 |
|---|---|---|
| 工程路径 | `workbench/mspm0/car/`（**必须全 ASCII**——GCC 不认中文路径） | 2026-07-26 |
| 编译 | 在 `car/gcc/` 下 `mingw32-make`（或 VS Code `Ctrl+Shift+B`） | 2026-07-26 |
| 工具链 PATH | `ARM GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin` + WinLibs `mingw64\bin`（见 `car/.vscode/tasks.json`） | 2026-07-26 |
| 烧录（推荐） | `car/flash.ps1`（**2026-07-27 改两段式**：① `program car.out exit` 只写不校验；② 另起只读会话 `init/halt/verify_image` 做字节比对）。openocd `xpack-openocd-0.12.0-7` + `interface/cmsis-dap.cfg` + `target/ti_mspm0.cfg`，**adapter speed 500**。**成功判据 = `** Programming Finished **` + `verified NNNNN bytes`**；看到裸 `Verify Failed` 先证伪、**别重烧**（见坑库同名条） | 2026-07-27 |
| 无头 SWD 调试 | `car/dbg.ps1 probe|registers|run-to-symbol`（包装 `.kiro/skills/mspm0-ccs/scripts/openocd_debug.py`）— **halt 会让 PWM 冻在当前占空，先发 `z` 停机** | 2026-07-27 `待真机` |
| 救砖 | `car/unbrick_flash.ps1`（**解锁+烧录必须同一 openocd 会话**，响铃时停点 RST） | 2026-07-26 |
| **改完 `.syscfg` 先自检** | `car/syscfg_check.ps1`（静态体检 + 临时目录试生成；**钉死 SDK `imports.mak` 的 `SYSCONFIG_TOOL`，本机有两个 1.28.0，用错版本等于白验**）— 实测 `status=ok`、无 warning | 2026-07-27 **PC 已验** |
| **查官方例程怎么配外设** | `car/sdk_find.ps1 <MODULE> [-Grep kw] [-AllBoards]`（索引本机 SDK 1765 个 `.syscfg` + `.meta/*.syscfg.js` 字段真源）—— 加外设前先查，别凭记忆猜 | 2026-07-27 **PC 已验** |
| ADC 同步定相采样参考例程（电流环前置） | `C:\ti\mspm0-sdk\examples\nortos\LP_MSPM0G3507\driverlib\adc12_triggered_by_timer_event`（多路同步 = `adc12_simultaneous_trigger_event`；整套 = `motor_control_pmsm_sensorless_foc\*`） | 2026-07-27 |
| 调试串口 | **COM30** @115200（DAP 的 VCOM；号会随拔插变，先扫端口） | 2026-07-26 |
| 串口发命令 | `car/uart_send.ps1`（**逐字符 + 25ms 间隔**——一次突发写会撑爆 MCU RX FIFO 丢字节） | 2026-07-26 |
| 固件命令集 | `m0..m5` 模式 / `t<v>` 目标 / `p,i,d<×1000>` 增益 / `w,e` 位置精定位 / `f<ms>` 遥测周期 / `x,y` DUAL 直驱 / `g` IMU / `z` 停 / `?` 状态 | 2026-07-26 |

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
