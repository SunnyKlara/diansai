# 天猛星 AI 调试工具链 · 借鉴 mspm0-skill（2026-07-27）

> **来源**：[mc3545dada/mspm0-skill](https://github.com/mc3545dada/mspm0-skill)（MIT License，上游 commit `d39c0c4` / 2026-07-25）。
> 作者环境与我们高度重合：**同一块立创天猛星 MSPM0G3507 + CMSIS-DAP/DAPLink + OpenOCD**（他另有 CCS/Keil/J-Link 路径，我们不用）。
> **已整份 vendor 进 `.kiro/skills/mspm0-ccs/`**（含 LICENSE），后续对话可直接激活取用；本文只记「我们借了什么、接到哪、验到哪一步」。
> 内容有改述以符合许可要求。

---

## 一句话结论

借到的是两类东西：**① 补能力盲区——AI 能自己用 SWD halt / 读寄存器 / 打断点**（此前只有「烧录 + UART printf」，固件跑飞/卡死/进不了 `main` 时 printf 什么也说不出，只能靠猜，double-fault 锁死那次就这么烧掉半天）；**② 补"查证据"的效率——把"凭记忆猜 SysConfig 字段 / 手工翻例程"换成"秒级检索官方真源 + 改完先本地试生成"**。

> **自我纠正（2026-07-27 第二轮）**：本文初版把第 ② 类轻描淡写成"我们已有或更强"，那是防御性自评、判错了。真跑一遍就打脸——SDK 检索工具**第一次用就找到了我们判定为"深水区、前置不满足"的电流环所需的官方例程**（`adc12_triggered_by_timer_event`，一直躺在本机硬盘里没被找到）。**元教训：评估外部工具要跑，别读文档下结论。**

| 借鉴项 | 对我们的价值 | 验证状态 |
|---|---|---|
| **OpenOCD+GDB 无头调试**（halt / 读 PC·SP·LR / 符号断点） | ⭐ 新能力，填盲区 | 链路已验证到 openocd 启动+cfg 加载+失败分类；**真机 halt 待板子接上验证** |
| **失败分类 + 速度回退梯**（探针没找到 / 传输抖 / 目标锁死 / verify 失败 各自处理） | ⭐ 直击我们「把探针问题当固件问题反复改代码」的历史 | 已实测：无板时正确判为 `probe_not_found` 并明说「这不能证明固件命令有错」 |
| **SDK 例程/元数据检索**（按 board+module 索引本机 1765 个 `.syscfg`） | ⭐ **被我低估过**：凭记忆找例程 → 秒级检索；已直接产出电流环所缺的官方参考 | **PC 已验**：接成 `car/sdk_find.ps1`（见 §3.2） |
| **改完 `.syscfg` 先本地试生成**（只写临时目录、不碰工程产物） | 高：把 syscfg 写错的发现点从"编译/真机"提前到"编辑后 10 秒" | **PC 已验**：接成 `car/syscfg_check.ps1`，实测 `status=ok`、无 warning |
| `check_syscfg.py` 工程静态体检 | 中：开工前 10 秒扫一遍（.syscfg/生成名/init 拼写/烧录产物） | **已在 `workbench/mspm0/car` 跑通**，输出真实结论 |
| **开发默认流程 + 证据阶梯**（不猜字段、模糊参数要问、分级报告验证程度） | 中高：把我们零散的习惯收敛成可执行流程（见 §5.2） | 规则类，已抄进本文 |
| 天猛星**特殊引脚**清单 | 中：影响四驱改版方案 B 选脚（见 §4） | 外部声明，**我们未核实** |
| UART **DMA TX** 替阻塞发送 | 高，但**本届不动**（见 §6） | 他已真机验证；我们未做 |

---

## 1. 已接好：`workbench/mspm0/car/dbg.ps1`

薄封装，把我们这块板的真实路径（xpack openocd / `target/ti_mspm0.cfg` / `gcc/car.out` / arm-none-eabi-gdb）喂给 vendor 进来的 `openocd_debug.py`。在 `car/` 目录下跑：

```powershell
.\dbg.ps1 probe             # 连上 -> halt -> 打印 target 状态 -> resume（不改 flash）
.\dbg.ps1 registers         # halt -> 打印 PC/SP/LR/xPSR -> resume   <- 卡死/跑飞第一手证据
.\dbg.ps1 run-to-symbol                      # reset 后停在 main，打印寄存器 + backtrace
.\dbg.ps1 run-to-symbol -Symbol encoder_poll # 停在任意函数
.\dbg.ps1 flash             # 烧 gcc/car.out + verify + reset run（备用，见下方提醒）
```

**验证状态（诚实）**：脚本本身、参数装配、openocd 启动与 cfg 加载、失败分类**已实测**（无板时正确报 `probe_not_found`）；**凡需要目标在场的动作（halt / 读寄存器 / 断点 / 烧录）一律 `待真机验证`**。
**烧录仍以 `flash.ps1` 为主**（真机用过无数次）：`dbg.ps1 flash` 走的是 `flash write_image erase` + `verify_image`，而 `verify_image` 的 CRC helper 在我们这块板上 halt 不稳时会报**假 diff**（见 `跨题坑库` "openocd 疑似校验失败先 mdb 证伪"）；且 MSPM0 烧完 `reset run` **起不来 app，必须物理按 RST**。

**接的时候踩到 3 个必须手工指定的点**（照搬上游默认会直接失败，记下来别再踩）：

| 坑 | 上游默认 | 我们必须 |
|---|---|---|
| target cfg 名 | 只认 `target/ti/mspm0.cfg` 找不到就用它 | 我们的 xpack 里是 **`target/ti_mspm0.cfg`** → 显式 `--target` |
| openocd scripts 目录 | 从 `<prefix>/share/openocd/scripts` 猜，且不给 openocd 传 `-s` | xpack 放在 `<prefix>/openocd/scripts` → 用 **`$env:OPENOCD_SCRIPTS`** 喂进去 |
| 固件产物位置 | 只找 `Debug/ Release/ build/ cmake-build-*/` 和工程根 | 我们在 **`gcc/car.out`** → 显式 `--program` |

**安全线（控制类板子必须守）**：`probe`/`registers` 会**把核 halt 约 1 秒**——控制环停摆、**PWM 冻在最后那个占空比上（电机会继续按原占空转！）**，之后才 resume。要在电机转着的时候看寄存器，先用 UART 发 `z` 停机再 halt。`run-to-symbol` 会 reset（电机停）。**一个探针同一时刻只允许一个 openocd 操作**——本役被后台循环 openocd 拖死 DAP USB 的账，就是这条。

**救砖策略不变**：这个 helper 明确**不会**自动 mass-erase / factory-reset；报 `target_locked_or_protected` 就停手，改用我们的 `unbrick_flash.ps1`（要手点 RST 配合，见 `编译烧录操作手册.md` §8）。这点和我们的血泪结论一致——自动救砖会把「无线/接触抖动」误判成锁死然后把 flash 擦了。

---

## 2. 已跑通：工程静态体检

```powershell
$env:PYTHONIOENCODING="utf-8"; [Console]::OutputEncoding=[System.Text.Encoding]::UTF8   # 不设=中文输出乱码
python ..\..\..\.kiro\skills\mspm0-ccs\scripts\check_syscfg.py .
```

本工程真实输出（2026-07-27）：

- `OK` 找到 `car.syscfg`、`@cliArgs` 元数据在、`SYSCFG_DL_init` 大小写与生成头一致、找到 `gcc\car.out`；
- `INFO` 判定工程为 `framework_multi_module`（认出了 `pc_test`）；
- `WARNING` **`.syscfg` 未声明 `@versions`** → 换机/换 SysConfig 版本时生成结果可能漂，值得补；
- `WARNING` 缺 `Debug/` 与 `targetConfigs/*.ccxml` → **我们走 gcc+openocd，这两条对我们无效**，无需处理。

**局限（别高估）**：它的引脚检查只是列出 `assignedPin=<数字>`，**不带端口上下文**（分不清 PA10 还是 PB10），所以「特殊引脚」那条规则靠的是 SKILL.md 里的文字规则、不是代码判定。我们工程列出的 10/11/14/26/22 全是 **PB** 口的 LCD/状态灯脚，不是 PA10/PA11 冲突。

---

## 3. 已跑通（PC 侧真验证）：改 .syscfg 前后的两件工具

### 3.1 `car/syscfg_check.ps1` — 改完 syscfg 先本地过一遍，别等编译/真机才炸

```powershell
.\syscfg_check.ps1              # 静态体检 + 用编译同一个 CLI 在临时目录试生成
.\syscfg_check.ps1 -StaticOnly  # 秒回，只做静态体检
```

**2026-07-27 实测结果**：`status=ok returncode=0 generated_files=8`，`car.syscfg` 当前生成**干净（只有 3 条 info，无 warning）**：ADC 自动关断模式下唤醒时间要计入采样窗、SPI/PWM 在 STOP/STANDBY 不保寄存器。生成只写临时目录，`gcc/ti_msp_dl_config.*` 不动。

**接的时候踩到的坑**：本机装了**两个** SysConfig 1.28.0（`C:\ti\sysconfig_1.28.0` 独立版 + `C:\ti\ccs2100\...\sysconfig_1.28.0`），而 `car.syscfg` 没写 `@versions` → 上游脚本拒绝猜、直接报错。已在 wrapper 里**钉死 SDK `imports.mak` 的 `SYSCONFIG_TOOL`（= 编译真正用的那个）**——**用另一个版本验证等于白验**，这点比"能跑起来"更重要。

价值：我们手改 syscfg 的历史坑（新模块要同时进 makefile+syscfg、4 线 CS 必须留一个硬件 CS、单端口 GPIO 宏名）全是**编译或真机阶段**才炸，现在能提前到编辑后 10 秒。

### 3.2 `car/sdk_find.ps1` — 加新外设前先查官方例程怎么配（我上一轮低估了这个）

```powershell
.\sdk_find.ps1 ADC12                 # 本板所有用 ADC12 的例程
.\sdk_find.ps1 ADC12 -Grep trigger   # 只看含关键字的
.\sdk_find.ps1 QEI -AllBoards        # 本板没有就看别的板
```

把本机 SDK 里 **1765 个 `.syscfg` 按 board+module 索引**（并列出 `.meta/*.syscfg.js` = SysConfig 字段真源）。我们原来的做法是手工 grep SDK 目录 + 凭记忆找例程名。

**⭐ 第一次用就有实打实收获——找到了我们判定为"深水区、前置不满足"的电流环所需的官方参考**：

| 我们卡住的东西 | 官方例程（本板 G3507，就在本机 SDK 里） |
|---|---|
| **ADC 触发同步 PWM 定相采样**（电流环前置①） | `examples\nortos\LP_MSPM0G3507\driverlib\adc12_triggered_by_timer_event`（+ `_stop` 变体） |
| 多路同时同步采样 | `driverlib\adc12_simultaneous_trigger_event` |
| 工业级 ADC-PWM 同步 + 电流环整套 | `motor_control_pmsm_sensorless_foc\*`、`motor_control_universal_foc\*` |
| 硬件正交解码（对照用；我们用软件采样解码抗 EMI 更好） | `driverlib\timg_qei_mode` |
| ADC 读数直接驱动 PWM 的最小闭环 | `msp_subsystems\adc_to_pwm` |

也就是说：**「电流环反馈要 ADC 同步定相采」这个结论我们靠真机实测挣出来了，但"怎么配"的官方样板一直在本机硬盘里没被找到。** 这条就是"我们已经有了"这句自评的反例。

---

## 4. 板级新情报：天猛星特殊引脚（外部声明·未核实）

上游 skill 称 LCKFB 天猛星文档把 **PA21 / PA23 / PA02 / PA18 / PA10 / PA11** 标为特殊脚、非必要不用；其中 **PA10/PA11 是板子默认 UART**（我们 UART0 调试串口正是 PA10/PA11，一致，无冲突）。

**对我们的实际影响 = 四驱改版方案 B 的选脚**：`四驱改版_接线设计.md` 里逃生口方案 B 用 **M3→TIMG7 (PA17/PA18)、M4→TIMG6 (PA21/PB27)**，**PA18 与 PA21 都在上述特殊脚名单里**。

- 定版走的是**方案 A（输入并联扇出、零新引脚）**，所以**当前不受影响**；
- 但**真要翻 0Ω 跳线切方案 B 之前**，必须先核实 PA18/PA21 到底特殊在哪（怀疑与 BSL/复位期行为或板上既有连接相关，**未证实**），必要时换脚。已在 `四驱改版_接线设计.md` 就地标注。

> 我尝试用公开检索核实这份名单，没找到权威出处；本地已抽的数据手册引脚页 dump 也只有脚名没有属性列。**结论：当外部提示用，不当定论。** 核实途径 = LCKFB 天猛星板级文档 / TI MSPM0 BSL 文档。

---

## 5. 纪律条款（他写成规则、我们是踩出来的，互相印证）

这几条上游写进了 skill 规则，我们各自都用真机代价买过一次，抄下来当铁律：

1. **探针问题 ≠ 固件问题**：`unable to find a matching CMSIS-DAP device` 是探针发现失败，不是你代码/链接脚本错——先别改代码。
2. **探针检测为空 ≠ 没插探针**：DAPLink/CMSIS-DAP 常是复合设备，可能只出现在 `USBDevice`/`HIDClass`/串口下（我们本次 `detect_probe.py` 就报空，实际是板子确实没插）。要下「没连」的结论前，先看 PnP + 串口列表 + 后端自己的只读 probe 命令。
3. **一个探针只跑一个 openocd 操作**，flash / 读寄存器 / GDB 会互相抢。
4. **锁死了停手问人，不自动擦片**。
5. **SysConfig 有 warning 就不许说"生成干净"**，warning 与 build/flash 成功分开报。
6. **分级报告验证程度**：源码静态 → SysConfig 生成 → 编译链接 → 烧录成功 → 板上行为 → 串口/示波器观测，**逐级独立**。这条和本仓库北极星（没真机验证一律标 `待验证`）是同一件事。

### 5.2 开发默认流程 + 证据阶梯（直接当我们的 MSPM0 开工流程用）

这是 skill 里**最值得整套搬过来的"开发方法"**部分——我们零散有这些习惯，但没收敛成流程。

**改任何 MSPM0 外设，按这个顺序走**：

1. 定位三件套：`car.syscfg`、生成的 `gcc/ti_msp_dl_config.h`、构建入口（我们=`gcc/makefile` + openocd，不是 CCS/`.ccxml`）。
2. `.\syscfg_check.ps1 -StaticOnly` 先扫一遍。
3. 读 `.syscfg` 元数据：器件 / 封装 / SDK / 模块 / 引脚 / 时钟 / 中断（别动 `@cliArgs`、`$assign`、`$suggestSolution`）。
4. **读生成头拿真实宏名和 init 拼写，不猜**（我们已经栽过：单端口 GPIO 实例是 `GPIO_X_PORT`、不是每脚 `GPIO_X_Y_PORT`）。
5. 不熟的字段**先取证**（见下方阶梯），禁止凭记忆造字段/枚举。
6. 只改最小面：`.syscfg` + 应用码；**新模块记得同时进 `makefile` OBJECTS + 规则**（老坑）。
7. `.\syscfg_check.ps1` 试生成 → 有 warning 就不算干净，单独报。
8. `mingw32-make` 编译 → 探针确认在位 → 烧 → **物理按 RST 冷启动**再看现象。

**证据阶梯（写不熟的 SysConfig 字段 / DriverLib 调用时按序取证）**：
① 本工程现有 `.syscfg` 里的同类实例 → ② `.\sdk_find.ps1 <MODULE>` 找到的官方例程 `.syscfg` + 同目录 `.c` → ③ SDK `source/ti/driverlib/.meta/<MODULE>.syscfg.js`（**字段/枚举/求解规则的真源**）→ ④ SysConfig GUI 对同器件生成一份对照 → ⑤ skill 的 `assets/snippets/`。

**哪些参数不该自己拍**（拍错=烧硬件或白调半天）：引脚 / 外设实例 / 波特率与帧格式 / 定时器周期 / PWM 频率与极性 / ADC 通道·参考·采样时间 / DMA 方向与源目的 / 中断优先级 / **外部模块的供电电压与逻辑电平**。
> 与本仓库「自主执行铁律」的边界：**执行细节（方案 A/B、命名、默认值）自己拍板不请示；上面这些"硬件后果不可逆"的参数缺失时问一句**——两者不冲突（后者正是"重大方向预警"那一类）。5V 编码器直连 3.3V 脚那次，就是缺"供电几 V / 怎么接"这个事实还硬开方的代价。

**接外部模块前先要齐**：数据手册 / 原理图或接线表 / 供电电压 / 逻辑电平 / 协议 / 地址或模式脚 / 关键时序。反复失败而 SysConfig+编译+烧录+逻辑都看着对时，**明确把怀疑指向接线·供电·地·上拉·电平转换·片选·TX-RX 交叉·I2C 地址·SPI 模式**，而不是继续改代码（我们双路电流那次根因就是虚接，改了半天代码）。

---

## 6. 值得做但**本届不动**：把遥测 UART 从阻塞发送改 DMA TX

上游有真机验证过的 **双 UART + DMA TX + 中断 RX** 例程（`.kiro/skills/mspm0-ccs/examples/uart_dma_tx_irq_rx/`）。

**为什么和我们特别相关**：我们被**阻塞式 UART 遥测拖慢主循环**这一类 bug 咬过两次——① 编码器采样放主循环被 stall → 漏拍/混叠、速度乱跳，环整不动；② 主循环「数拍×假设1ms」计时被 LCD 重绘+串口拖慢 → RPM 虚高约 5 倍。两次都是**用"把采样搬进 SysTick 中断/用真实时基"绕过**，而没有根治「发送本身会 stall 主循环」。DMA TX 是根治法。

**但现在（距省赛 2 天）不动**：速度环+位置环刚真机达标锁死（`4bbaae5`/`3a4c6f5`），改遥测通道要动 syscfg（DMA 通道/中断）+ `uart_dbg.c` + 所有整定脚本依赖的输出格式，回归面积大、收益是「性能余量」而非「当前缺陷」。**决定：记为省赛后改进项**；若赛中再次出现遥测拖慢控制环（症状：`f<ms>` 调快后指标变差），再按此例程改，且必须单变量验证。

另附他的其余例程当参考（PWM 呼吸灯用 CCP1、TIMG12 1ms 中断在 80MHz 下 load=79999、改 CPUCLK 后必须重看生成头的 load 值）：`.kiro/skills/mspm0-ccs/examples/` + `references/hardware_validation_notes.md`。

---

## 7. 我们有、它没有（反向对照，别照搬丢了自己的东西）

| 我们的做法 | 为什么不能被替代 |
|---|---|
| UART 命令**逐字符发 + 25ms 间隔** | 我们的 MCU RX FIFO 小，一次突发会丢字节（`t100` 这种长命令必丢）。上游 `serial_console.py` 是一次性 `write`，**没有这层保护** |
| `tune_step.ps1` / `pos_step.ps1` / `disturb_test.ps1`：**一键跑一轮 + 自动算 avg/std/超调/上升时间** | 上游串口工具只是「收发+时间戳」的哑终端，没有定量裁决层——而定量裁决正是我们调参能收敛的原因 |
| `unbrick_flash.ps1`（解锁+烧录**同一 openocd 会话**、成功即响铃让人精确停点 RST） | 上游明确不做自动救砖；我们这套是真机救回过一次的 |
| 编码器**定时采样 4x 正交解码**（不吃 GPIO 边沿中断） | EMI 环境下边沿中断会被毛刺打成中断风暴（实测 I≫E 50 倍）；这是我们的真机结论，上游没有 |
| `.kiro` 记忆体系 + 诚实标注铁律 | skill 是「怎么用工具」，我们这套是「怎么不失忆、不吹成果」 |

**未采纳**：CCS-DSS / DSLite / J-Link / Keil / CMake 那几条路径（我们不用）；他 `examples/` 里的 `BSP/` 目录结构（我们工程是扁平 `car/`，上游自己也写了「不要把示例目录结构塞进用户工程」）。

---

## 8. 下一步（真机相关，等板子接上）

1. 板子上电 + DAP 插好 → `.\dbg.ps1 probe` 应看到 target 状态；再 `.\dbg.ps1 registers` 看 PC/SP/LR。**这一步跑通才算这条工具链真机验证通过**，跑通后把实测输出补进 `调试日志.md`。
2. `.\dbg.ps1 run-to-symbol -Symbol main` 验证 GDB 断点链路（会 reset，电机停，安全）。
3. 赛中若遇「烧进去了但没反应 / 卡死 / 屏黑」——**先 `registers` 看 PC 落在哪**（落在 `HardFault_Handler`、落在 `0xFFFFFFFE`、还是根本没出 startup），比重烧一遍猜有效得多。
