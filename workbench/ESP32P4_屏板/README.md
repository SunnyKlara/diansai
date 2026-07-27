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

| 优先级 | 测试项 | 状态 |
|---|---|---|
| ✅ | 背光归属 / 触摸映射 / 显示受控 / 自绘 UI | 已定论，见第三节 |
| ✅ | **flash 4MB→16MB + 分区放大** | `SPI Flash Size : 16MB`，factory 8M + storage 6M(FAT)；旧固件那条 `Detected size larger than image header` 警告消失 |
| ✅ | **QMI8658A 六轴** | 地址探测 **0x6A**（0x6B 无应答）· `WHO_AM_I=0x05` · 静止 **\|a\|=984~991mg**(目标 1000±50) · 静止陀螺 ≈0 · 31℃ · 平放时 **Z 轴朝上** |
| ✅ | **LVGL 跑分**（1280×452 RGB888 + PPA 硬旋转 + PSRAM 双局部缓冲） | ai_panel 稳定 **60.5~63 FPS**；benchmark 轻场景 57~63.5（撞 `LV_DEF_REFR_PERIOD=15ms≈66Hz` 上限）、重场景(大面积渐变/图像/阴影) **11.5~17**；全程零 panic/WDT |
| ⏳ | **microSD** | 驱动+测速代码已写、编译通过、真机跑到挂载即返回 `ESP_ERR_TIMEOUT`(`send_op_cond` 超时 = 卡无响应，判为**未插卡**)。**挂载成功路径与测速代码 `待插卡验证`** |
| P2 | 摄像头（需 CSI 模组 + 24P 排线）· 放音（需 MX1.25-2P 喇叭；录音现在就能测）· 电池电量/充电（需锂电）· USB OTG host（U 盘/UVC） | 额外硬件 |
| P3 | ESP32-C5 WiFi6(2.4G/5G)+BLE via ESP-Hosted（需给 C5 单独刷 slave 固件，走 6P BOOT 排针）· P4 的 H264/JPEG 硬编解码 | 复杂 |

**踩过的两个软件坑（都记进坑库了）**：
1. **I2C 控制器抢占**：原理图标 `I2C_SDA0/SCL0`，照抄成 `I2C_NUM_0` → 与触摸驱动（`TP_I2C_PORT=I2C_NUM_0`，引脚 IO28/29）撞车，IMU 抢到控制器后触摸 `i2c_new_master_bus` 失败、每帧轮询报错刷屏 88 条 `clear bus failed`。**原理图的总线编号 ≠ ESP-IDF 的 `I2C_NUM_x` 控制器编号**，IMU 改用 `I2C_NUM_1` 后两者共存。
2. **`i2c_master` 句柄不能多任务并发**：`app_main` 与 LVGL 定时器各读一次 IMU 就把总线状态机搞崩。现在**全工程只有 `ai_panel` 的定时器读 I2C**，串口判据也从那里打。
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
| `firmware/ai_panel.c` · `ai_panel.h` | **原创**：自绘验证面板（触摸跟手圆点 / IMU 实时读数 / SD 状态 / 背光判决实验，`AI_PANEL_BL_TEST` 默认 0） |
| `firmware/imu_qmi8658.c` · `.h` | **原创**：QMI8658A 六轴驱动（I2C 地址自动探测 + 定标换算 + 静止判据） |
| `firmware/sdcard.c` · `.h` | **原创**：microSD（SDMMC slot0 4-line）挂载 + 顺序读写测速 |
| `firmware/upstream_local.patch` | 对上游 8 个文件的全部改动（`main/CMakeLists.txt` / `lcd_panel_select.h` / `lvgl_demo.c` / `lvgl_demo.h` / `main.c` / `partitions.csv` / `sdkconfig` / `sdkconfig.defaults`，共 129 insertions） |

> 拷源码时别漏文件：`Copy-Item ..\..\ESP32P4_屏板\firmware\*.c,..\..\ESP32P4_屏板\firmware\*.h main\`

**换机重建步骤**：

```powershell
# 1. 取上游（路径必须全 ASCII 且短，ESP-IDF 不吃非 ASCII 路径）
git clone -b mipi_lcd https://gitee.com/Ergou-/esp32-p4-c5-aibox.git d:\diansai\workbench\esp32p4\p4_lcd
cd d:\diansai\workbench\esp32p4\p4_lcd

# 2. 打回本地改动 + 放回原创源码（ai_panel / imu_qmi8658 / sdcard 共 6 个文件）
git apply "..\..\ESP32P4_屏板\firmware\upstream_local.patch"
Copy-Item "..\..\ESP32P4_屏板\firmware\*.c","..\..\ESP32P4_屏板\firmware\*.h" main\

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

## 八、无线图传方案评估（2026-07-27 · 资料核实，**未动代码、未上板**）

> 起因：问"这块 P4 板能不能和 K230 一起做无线图传"。下面的能力数字全部来自官方文档（已内联出处），**不是本板实测**；本板真机验过的东西在第一、二节。

### 8.1 两边能力对照（决定架构的关键）

| 能力 | ESP32-P4 | K230 |
|---|---|---|
| 摄像头输入 | MIPI-CSI（板上 24P 座已布线，模组待买） | MIPI-CSI |
| H.264 **编码** | **硬件，1080P@30fps**（[esp_h264](https://components.espressif.com/components/espressif/esp_h264/versions/1.2.0~1/readme)） | 硬件 H.264/H.265 |
| H.264 **解码** | ⚠️ **软件**（tinyH264），性能差 | 硬件 |
| JPEG | **硬件编解码都有**（[IDF jpeg 外设文档](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32p4/api-reference/peripherals/jpeg.html)，官方例程解 1080p/720p） | 硬件 |
| NPU | **无** | **有 KPU** —— P4 给不了的东西 |
| 无线 | 靠 ESP32-C5 协处理器走 SDIO | CanMV 有 [WLAN 例程](https://developer.canaan-creative.com/k230_canmv/en/main/example/network/wlan.html) |
| 屏 | ✅ 本板已点亮 1280×452 / 60+FPS | 需外接 |

**链路带宽不是瓶颈（含端到端 iperf 实测，比 raw 吞吐更有用）**——[esp-hosted-mcu README 传输对照表](https://github.com/espressif/esp-hosted-mcu/blob/main/README.md)：

| 传输 | Host Tx (Mbps) | Host Rx (Mbps) | 备注 |
|---|---|---|---|
| **SDIO 4-bit** | udp 79.5 / **tcp 53.4** | udp 68.1 / **tcp 44** | 最高性能；**官方标注"PCB only"**——跳线不行，本板是 PCB 走线 ✅ |
| 标准 SPI | udp 24 / tcp 22 | udp 25 / tcp 22 | 快速验证用 |
| UART | 0.68 | 0.68 | 只够低速数据，不可能图传 |

- **ESP32-C5 明确在协处理器支持列表内**（ESP32 / C2 / C3 / **C5** / C6 / S2 / S3），且 **SDIO 4-bit 的支持芯片正好含 C5**。
- 另有 SDIO 内部 raw 吞吐数据（streaming 80 / packet 33 Mbit/s，见 [sdio.md](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/sdio.md)）——那是链路层裸速，**做带宽预算要用上表的端到端 iperf 数字**。
- ⇒ **P4 接收方向可用约 TCP 44 / UDP 68 Mbps**，是方案 D 的带宽上限。

> ⇒ **核心判断**：只要"无线图传"，**P4 单板就够，用不上 K230**；要"识别"才需要 K230 的 NPU。两者合理分工**不是"K230 传图给 P4"**，而是 **K230 当眼睛+大脑、P4 当屏幕+电台**（传大图再转发是绕路）。

### 8.2 四种架构（按工作量排序）

| 方案 | 链路 | 评价 |
|---|---|---|
| **A** K230 单干 | K230 采集+编码+WiFi → PC/手机 | 最省事，CanMV 例程即可，**P4 不参与** |
| **B** P4 单干 ⭐ | CSI 摄像头 → P4 硬件 H.264 → RTSP → C5 WiFi | 链路最短、全官方组件、正是 P4 硬件编码器的设计用途；**无 NPU 不能识别**；需买 CSI 模组 + 24P 排线 |
| **C** 协同 ⭐ | K230 识别 → 有线(UART/SPI) → P4 显示 + C5 转发遥测 | K230 只传**结果+小图**，带宽需求低、最稳，也最贴合赛题真实需要 |
| **D** 无线图传到本地屏 | K230 → WiFi → C5 → SDIO → P4 解码 → 屏 | 可行但**必须用 MJPEG 不能用 H.264**（P4 只有硬件编码器、解码是软件的）；四者中工作量最大 |

### 8.3 三个真实的坎（动手前必须知道）

1. **C5 的 WiFi 在本板从未验证过** —— 要给 C5 单独刷 ESP-Hosted slave 固件，走顶部 6P `BOOT` 排针（`C5_TXD0/RXD0/EN/BOOT`）。这是 B/D 的共同前置，从零开始的一整块工作。
2. **C5 的 SDIO 会和 microSD 抢 SDMMC 控制器** —— 原理图上 C5 SDIO 走 **GPIO14-19**（`C5_D0..D3=IO14..17 / C5_CLK=IO18 / C5_CMD=IO19`，`待生成配置核对`），不是 slot0 的 IOMUX 脚(39-44)，所以它得走 GPIO matrix 用 **slot1**，而 SD 卡占着 **slot0**。官方正好有例程 **`mcu_hosted_sdio_sdmmc_combined`**（同一 SDMMC 控制器、两 slot 分别跑 ESP-Hosted 与 SD 卡），照它做。
3. **console 占着 GPIO37/38，而那正是 P4↔C5 的 UART** —— 启动日志明写 `GPIO 38 and 37 are used as console UART I/O pins`。用 C5 前要先把 UART console 关掉、只留 USB-JTAG。

### 8.4 对电赛的定位（别高估）

图传**对多数赛题不加分**：赛题要的是"识别结果驱动执行机构"，不是把画面传出来。且赛场几十支队伍同时开热点，无线图传实测会卡到不可用。**图传的价值在调试与演示**，正式方案里感知链路应走有线。真要用 K230+P4，选**方案 C**。

### 8.5 方案 D 的阻力清单（2026-07-27 逐项核实，按拦路程度排序）

> D = K230 采集编码 → WiFi → 板载 C5 → SDIO → P4 解码 → MIPI 屏显示。
> **结论：没有死路，全部是工作量而非不可行。** 下面每条都给了"为什么"和"怎么绕"。

| # | 阻力 | 性质 | 处理 |
|---|---|---|---|
| 1 | **C5 的 WiFi 在本板从未验证** | 最大工作量，**非死路** | ESP-Hosted 官方支持列表含 **ESP32-C5**；需给 C5 刷 slave 固件（走顶部 6P `BOOT` 排针）+ P4 侧配 `esp_hosted`/`esp_wifi_remote` |
| 2 | **C5 SDIO 与 microSD 争 SDMMC 控制器** | 配置问题 | C5 走 GPIO14-19（非 slot0 IOMUX）⇒ 用 **slot1**，SD 卡留 slot0；官方对症例程 `mcu_hosted_sdio_sdmmc_combined` |
| 3 | **console 占着 GPIO37/38 = P4↔C5 的 UART** | 一行配置 | 关掉 UART console、只留 USB-JTAG |
| 4 | **编码格式只能 MJPEG，不能 H.264** | 硬件限制，**必须遵守** | P4 只有硬件 H.264 **编码器**，解码是软件 tinyH264 ⇒ 收流显示走 **JPEG 硬解**；K230 侧输出 MJPEG |
| 5 | 带宽 | **不是阻力**（已算） | 可用 TCP≈44Mbps；1280×452 MJPEG@30fps 约 21Mbps（YUV420 868KB/帧、10:1 压缩估）、640×480@30fps 约 4Mbps ⇒ 余量充足 |
| 6 | P4 侧要写：socket 收流 + JPEG 硬解 + 送 LVGL | 常规开发 | IDF 有 `jpeg_decode` 例程 + `esp_lv_decoder`；LVGL 侧用 canvas/image |
| 7 | K230 侧要写：抓帧 + JPEG 编码 + socket 推流 | 常规开发 | CanMV(MicroPython) 有 WLAN 例程可打底 |
| 8 | 屏比例 **1280×452** 极端长条 | 体验问题 | 摄像头 4:3/16:9 画面贴上去会大量留白或裁切 |

**⚠️ 先想清值不值**：若目标只是"人能看到画面"，方案 A（K230 直推手机/PC 浏览器）几分钟就能出效果，且不受这块长条屏比例限制。**D 的真正价值是打通"C5 无线链路 + P4 硬件解码显示"这套技能栈**，不是画面本身。要练这套栈就做 D，要看画面就做 A。
