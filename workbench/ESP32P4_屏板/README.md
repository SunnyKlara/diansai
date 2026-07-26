# ESP32-P4 + 6.2寸 MIPI 长条屏（FRENIX 开发板 R5）

> **定位（诚实）**：与 2026 省赛材料清单**无交集**，不是省赛资产。价值在于**赛后玩具 / 国赛的"显示 + 视觉前端"储备**。
> 一个关键限制：**扩展排针只引出 5 个 GPIO**（IO1–IO5 + 3.3V/5V/GND）→ **当不了控制主控**（电机/编码器/传感器接不下）。
> 唯一说得通的赛用分工：**P4 做眼睛 + 屏（视觉识别、人机界面），MSPM0/STM32 做手脚，中间一根 UART**。

- 板子：`ESP32P4_4IN_AIBox R5 2026-06-18`，主控 ESP32-P4 + 无线协处理器 ESP32-C5，产品页 <https://www.wanglinfeng.fun/products/esp32p4.html>
- 屏：`HL062CTIPS-M`，6.2 寸 IPS，452×1280，驱动 IC **AXS15260D**，MIPI-DSI 2lane，触摸内置
- 参考工程（第三方 clone + 本地改动，**不入库**）：**`workbench/esp32p4/p4_lcd/`**（Gitee `Ergou-/esp32-p4-c5-aibox` 分支 `mipi_lcd`）
  - ⚠️ 路径必须**全 ASCII 且尽量短**：ESP-IDF/CMake 不吃非 ASCII 路径（同本仓库"MSPM0 放 `workbench/mspm0/` 因为 GCC 不认中文路径"那条铁律），IDF 编译路径又深、容易撞 Windows 260 字符限制。原先放在 `workbench/_参考工程/` 下，已因此挪走。
  - 本地改动（4 处，改动理由都写在文件注释里）：`main/lcd_panel_select.h` 选屏切 6.2 寸 · 新增 `main/ai_panel.c/.h` 自绘验证面板 · `main/lvgl_demo.h` 加开关 `LVGL_DEMO_AI_PANEL` · `main/main.c` 加 `BL_BLINK_TEST` 背光通断测试。

## 一、真机实测（2026-07-27 · 零安装阶段，当时还没装 ESP-IDF）

手段 = `uvx esptool`（只读）+ `tools/p4_boot_read.ps1`（RTS 硬复位 + 抓 boot log）。原始日志见 `资料/boot_log_出厂固件_2026-07-27.txt`。

| 项 | 实测值 |
|---|---|
| 调试口 | **丝印 `UART` 的 Type-C** → `COM7`，`VID_303A&PID_1001`，另有 `USB JTAG/serial debug unit`（丝印 `USB` 那个是 OTG-HS 功能口，烧不进去） |
| 芯片 | ESP32-P4 **rev v1.3**，双核 + LP 核，实跑 360MHz，晶振 40MHz，MAC `80:f1:b2:d1:35:2e` |
| Flash | **实测 16MB**（Boya mfr 0x68 / dev 0x4018）；但固件头只声明 2MB、分区表仅 `nvs 24K + phy 4K + factory 1MB` → 自编固件应改 16MB + 放大分区 |
| PSRAM | **32MB hex @200MHz**（AP，256Mbit，X16，XIP on PSRAM） |
| 出厂固件 | IDF **v5.5.4**，编译 2026-07-01 16:17；工程名 `test_esp_lcd_jd9165` = **厂家 fork 遗留名，实际跑 AXS15260**（别被骗） |
| 屏 | AXS15260 **452×1280** → LVGL 显示 **1280×452**（`sw_rotate` + PSRAM 双局部缓冲），与参考工程 `lcd_panel_axs15260_6_2in` 同源 |
| 触摸 | AXS15260 内置，**I2C 0x3B**，ID/固件版本 `0x0020`，`SDA=IO28 / SCL=IO29 / INT=IO27` —— 初始化成功 |
| 远程复位 | **RTS 脉冲可硬复位**（日志 `rst:0x17 CHIP_USB_UART_RESET`）→ AI 可自主重启抓日志，不需要人碰板子 |
| efuse | 无 secure boot、无 flash 加密、下载模式与 JTAG 均未禁、`WR_DIS=0` → **可自由烧写 + 可 JTAG 单步** |
| 稳定性 | 15s 被动监听零输出（无 panic / 无 WDT / 无重启）→ 200MHz PSRAM + XIP **短时**稳；长时间 `待验证` |
| console | 主 console 在 **GPIO37/38**（=接 ESP32-C5 的那条 UART！）；能从 USB-JTAG 看到日志靠 IDF 的 secondary console。**将来用 C5 做 WiFi 要把 console 改成直连 USB-JTAG**（参考工程本来就这么配） |

## 二、ESP-IDF 环境 + 自编固件（2026-07-27 已跑通）

- **ESP-IDF v5.5.4** 装在 `D:\esp32\Espressif\frameworks\esp-idf-v5.5.4`，复用既有 `IDF_TOOLS_PATH=D:\esp32\Espressif`。
- ⚠️ **本机原本就有一套官方 installer 装的 v5.1.2**（编不了 P4），机器级环境变量 `IDF_PATH` 仍指向它 → **动手前必须先 `. tools\idf_shell.ps1`**，别依赖系统环境。
- 安装踩了 5 个坑（中文用户名 / Python 无 CA 证书 / 特定 SNI 的 TLS 被切 / GitHub submodule 龟速 / idf_tools 文件名规则），全部记在 `.kiro/steering/knowledge/跨题坑库.md` 同名条，脚本注释里也留了"为什么这么写"。

| 已验证 | 结果 |
|---|---|
| 编译 | 1907 目标全过，`p4_mipi_lcd.bin` **872,848 B**，app 分区 3MB 占 28% |
| 烧录 | `idf.py -p COM7 flash` 全部 `Hash of data verified` |
| 板上固件身份 | `Project name: p4_mipi_lcd` + `Compile time` 对得上 → 确认跑的是自编固件，不是厂家 demo |
| 屏 + 触摸复现 | AXS15260 452×1280 → LVGL 1280×452（PPA sw_rotate）· 触摸 I2C 0x3B / ID 0x0020 / INT GPIO27 |
| 自绘 UI 上屏 | `ai_panel.c`：编译时间戳 + 运行秒数 + 心跳条 + **触摸跟手圆点** + 四角靶标 |
| 背光通断已执行 | `main.c` 主动 `GPIO30=0/1` 各 3 次，串口逐次打印（**屏黑没黑要人眼判**，见下） |

## 三、人眼判读（AI 看不到，2026-07-27 已答 2/3）

1. ✅ **触摸坐标映射正确** —— 用户实测"严丝合缝"，跟手圆点严格压在指尖下 ⇒ `lvgl_demo.c` 里 `rotation` 的 `swap_xy / mirror_x / mirror_y` **保持全 false 即正解，不用调**。串口定量佐证：`touch #1 at X=1058 Y=71`（落在 1280×452 坐标系、与手指位置一致）。
2. ✅ **显示链路 + 自绘 UI 确认** —— 屏上就是 `ai_panel` 画的界面（深色底/青色标题/黄色 BUILD 时间戳/四角红靶标），无花屏报告。
3. ✅ **背光归属已定论：`GPIO30` 就是背光控制脚** —— 用户实测"屏幕有按设定的定时黑屏"。⇒ **原理图 `LCD_BL = IO30` 正确；出厂固件里驱动 `GPIO47` 的那段是厂家 fork 遗留代码**（同一份固件的工程名 `test_esp_lcd_jd9165` 也是遗留，两处互相印证）。
   - 判决实验做了两版：v1（`main.c` 里闪 0.5s×3）**判不出**——窗口太短、且屏若不灭则"没看到"与"没执行"不可区分；v2（判据印在屏上 + 灭 3s + 每 33s 自动循环）**一次定论**。方法论已入 `跨题坑库.md`。
   - 结论既定，`ai_panel.c` 的 `AI_PANEL_BL_TEST` **已置 0 并重烧**（否则每 33s 闪一次背光会干扰使用），屏上改为常驻一行结论文字。要在别的板/别的屏重跑该实验，把开关改回 1。
4. ⬜ **长时间稳定性**：挂机 20–30 分钟后再抓一次日志看有无偶发重启（200MHz PSRAM + XIP 的问题常几十分钟才现形）；顺便读右下角 FPS/CPU 悬浮层（`LV_USE_PERF_MONITOR=y`）。

## 四、板载外设与测试矩阵

**已布线的外设（按原理图核过）**：MIPI-DSI 屏 30P · MIPI-CSI 摄像头 24P（OV2710 1080P，板载 24MHz 晶振供 XCLK，I2C 经 BSS138PS 做 1.8V↔3.3V 电平转换）· ES8389 音频 codec + 2×NS4150B 功放 + 2 喇叭座 + 板载双硅麦 · **QMI8658A 六轴 IMU（I2C0，IO7/IO8）** · microSD 4-line SDMMC · ESP32-C5-MINI-1-N4（SDIO slave + UART IO37/38 + 顶部 6P `BOOT` 排针给 C5 刷固件）· AXP2101 PMIC（**I2C 0x34，与触摸同一条总线**）+ 电池座 + PWRON 长按 · USB OTG-HS · 扩展排针 IO1–IO5。

| 优先级 | 测试项 | 前置 |
|---|---|---|
| P0 | 触摸映射、显示质量、FPS 读数、挂机稳定性 —— **当前卡在这里，见第三节** | 只需人眼 |
| ✅ 已做 | 背光 IO30 通断（已执行，等判读）· 触摸坐标打印 · LVGL 自绘 UI | IDF 已装好 |
| P1 | **QMI8658A 六轴读数**（对电赛最有用，姿态算法可复用天猛星 `attitude.c`）· microSD 挂载+读写 · `lv_demo_benchmark` 跑分（PPA 硬旋转 vs 软旋转）· flash 4MB→16MB + 分区放大 | 无（IDF 已就绪） |
| P2 | 摄像头（需 CSI 模组 + 24P 排线）· 放音（需 MX1.25-2P 喇叭；录音现在就能测）· 电池电量/充电（需锂电）· USB OTG host（U 盘/UVC） | 额外硬件 |
| P3 | ESP32-C5 WiFi6(2.4G/5G)+BLE via ESP-Hosted（需给 C5 单独刷 slave 固件，走 6P BOOT 排针）· P4 的 H264/JPEG 硬编解码 | 复杂 |

## 五、工具用法

```powershell
# ===== 编译 / 烧录（IDF 环境由脚本自动激活，不用手动 export）=====
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_p4.ps1                    # 只编译
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_p4.ps1 -Flash -Port COM7  # 编译 + 烧录

# 交互式开发时先激活环境，之后可直接用 idf.py
. tools\idf_shell.ps1
idf.py build ; idf.py -p COM7 flash

# ⚠️ 永远不要跑 idf.py set-target —— 它按 defaults 重生成 sdkconfig，
#    会静默丢掉防花屏的关键项（IDF_EXPERIMENTAL_FEATURES / PSRAM HEX 200M+XIP / TASK_WDT off / PPA）

# ===== 重装 IDF 时才需要（本机已装好，留档备查）=====
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch_idf_tools_via_curl.ps1  # curl 预取工具包
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_idf554.ps1            # 再跑安装

# 抓启动日志（RTS 硬复位后采集 8 秒）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\p4_boot_read.ps1 -Port COM7 -Seconds 8

# 被动监听（不复位，验证运行期有无 panic/重启）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\p4_boot_read.ps1 -Port COM7 -Seconds 60 -NoReset -Out run_log.txt

# 芯片尽调（只读，不改板子）
uvx esptool --port COM7 chip-id
uvx esptool --port COM7 flash-id
uvx --from esptool espefuse --port COM7 summary

# 备份出厂固件（动写操作前必做的安全网）
uvx esptool --port COM7 read-flash 0 0x200000 厂家固件备份\factory_full_2MB_2026-07-27.bin
# 还原：把上面这条的 read-flash 换成 write-flash 0 <文件>

# 解析 flash dump（分区表 + app 描述）
uvx esptool --port COM7 read-flash 0x8000 0x8100 dump.bin
python tools\parse_flash_dump.py
```

> 脚本设计遵循本仓库坑库：**一次性事务式**串口脚本（发/复位 → 采 N 秒 → 写文件 → 最后 Close，避免 `SerialPort.Close()` 在流数据下死锁）；全 ASCII 注释（避 PS 5.1 编码坑）；`DTR` 保持 de-assert 以免误进下载模式。
> `厂家固件备份/*.bin` 与参考工程 clone **不入库**（`.gitignore` 已覆盖），资料 PDF/图/日志入库（原理图曾因未入库随重装蒸发过一次，这次留档）。
> ⚠️ 工程 clone 不入库 ⇒ **我们对固件的改动必须另存**，见 **§六**（原创源码全文 + 上游改动 patch + 换机重建步骤），别只依赖那个 473MB 目录还在。

## 六、固件源码在哪 / 换机后怎么重建

⚠️ **工程本体 `workbench/esp32p4/p4_lcd/` 是第三方 clone（473MB，含 `.git` 与编译产物），按 `.gitignore` 不入库** ⇒ 换机 / 重装 / 误删后它**不会**随仓库回来。为此本目录 `firmware/` 存了**重建所需的全部差异**（本仓库"纸面不 commit 即蒸发"铁律，原理图 v1.3 已经这么丢过一次）：

| 文件 | 说明 |
|---|---|
| `firmware/ai_panel.c` · `ai_panel.h` | **我们原创**的自绘验证面板（含背光判决实验，`AI_PANEL_BL_TEST` 默认 0）。完整源码，直接入库 |
| `firmware/upstream_local.patch` | 对上游 5 个文件的全部改动（`CMakeLists.txt` / `lcd_panel_select.h` / `lvgl_demo.c` / `lvgl_demo.h` / `main.c`，共 47 insertions） |

**换机重建步骤**：

```powershell
# 1. 取上游（路径必须全 ASCII 且短，ESP-IDF 不吃非 ASCII 路径）
git clone -b mipi_lcd https://gitee.com/Ergou-/esp32-p4-c5-aibox.git d:\diansai\workbench\esp32p4\p4_lcd
cd d:\diansai\workbench\esp32p4\p4_lcd

# 2. 打回本地改动 + 放回原创源码
git apply "..\..\ESP32P4_屏板\firmware\upstream_local.patch"
Copy-Item "..\..\ESP32P4_屏板\firmware\ai_panel.*" main\

# 3. 装 IDF（若机器上没有）→ 编译烧录
#    见第五节：fetch_idf_tools_via_curl.ps1 → install_idf554.ps1 → build_p4.ps1
```

> patch 若因上游前进而冲突：改动只有 47 行、且每处都在文件注释里写明了意图，按下面第七节的清单手工重做即可。

## 七、工程改动清单

| 状态 | 项 |
|---|---|
| ✅ 已做 | `main/lcd_panel_select.h`：`LCD_PANEL_ACTIVE` → **`lcd_panel_axs15260_6_2in`**（原为 10.1 寸 JD9366） |
| ✅ 已做 | 新增 `main/ai_panel.c/.h` 自绘验证面板 + `main/CMakeLists.txt` 加源文件 |
| ✅ 已做 | `main/lvgl_demo.h` 新增 `LVGL_DEMO_AI_PANEL`（默认 1，优先级高于 `LVGL_DEMO_BENCHMARK`）；`lvgl_demo.c` 加载分支 |
| ✅ 已做 | 背光判决测试 **v2 在 `ai_panel.c` 内**（自解释判据 + 800ms 渲染窗口 + 3s 灭 + 33s 自动循环）。`main/main.c` 的旧 `BL_BLINK_TEST` **已默认 0**——挪走后必须关掉，否则两处抢同一个 GPIO |
| ⬜ 待做 | `sdkconfig` + `sdkconfig.defaults`：flash `4MB → 16MB`，`partitions.csv` 的 factory 从 3M 放大（板子有 16MB，现在只用 4MB）。⚠️ 改 defaults 不影响已存在的 `sdkconfig`，两处都要改并回读确认 |
| 🚫 别动 | `IDF_EXPERIMENTAL_FEATURES=y`（花屏根因）、`ESP_TASK_WDT_EN=n`、PSRAM HEX/200M/XIP、`FREERTOS_HZ=1000`、LVGL 任务栈 ≥16KB —— 上游作者标注"缺一不显示" |
