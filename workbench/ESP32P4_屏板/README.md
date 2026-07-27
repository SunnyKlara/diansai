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

## 八、无线图传方案评估（2026-07-27 · **本节只是资料核实**，真机结果在 §九）

> 本节的数字（带宽、能力对照）全部来自官方文档，**本节自身未上板**。
> 其中"C5 WiFi 能不能用"这一条**已被 §九 真机验证**（扫 51 个 AP + ping 5/5）；读本节时以 §九 为准。

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

1. ~~**C5 的 WiFi 在本板从未验证过**~~ → ✅ **2026-07-27 真机通过，本条作废**（扫 51 个 AP + ping 5/5）。**而且不需要 USB-TTL——C5 出厂自带 ESP-Hosted 协处理器固件。** 数据与剩余待办见 **§九**，别照本条的旧结论排工作量。
2. **C5 的 SDIO 会和 microSD 抢 SDMMC 控制器** —— C5 SDIO 走 P4 的 **GPIO14-19**（`C5_D0..D3=IO14..17 / C5_CLK=IO18 / C5_CMD=IO19`，**2026-07-27 已与 esp_hosted 内置板级预设逐脚核对一致，见 §9.4**），不是 slot0 的 IOMUX 脚(39-44)，所以它得走 GPIO matrix 用 **slot1**，而 SD 卡占着 **slot0**。⇒ **配置层面已解决**（`CONFIG_ESP_HOSTED_SDIO_SLOT_1=y`，实测生成的 sdkconfig 就是 slot 1）。要同时跑 SD 卡与 hosted，参考官方例程 **`mcu_hosted_sdio_sdmmc_combined`**。
3. **console 占着 GPIO37/38，而那正是 P4↔C5 的 UART** —— 启动日志明写 `GPIO 38 and 37 are used as console UART I/O pins`。→ **已处理**：两个 host 工程都设了 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`，日志仍从 COM7 出（COM7 本就是 USB-Serial-JTAG）。

### 8.4 对电赛的定位（别高估）

图传**对多数赛题不加分**：赛题要的是"识别结果驱动执行机构"，不是把画面传出来。且赛场几十支队伍同时开热点，无线图传实测会卡到不可用。**图传的价值在调试与演示**，正式方案里感知链路应走有线。真要用 K230+P4，选**方案 C**。

### 8.5 方案 D 的阻力清单（2026-07-27 逐项核实，按拦路程度排序）

> D = K230 采集编码 → WiFi → 板载 C5 → SDIO → P4 解码 → MIPI 屏显示。
> **结论：没有死路，全部是工作量而非不可行。** 下面每条都给了"为什么"和"怎么绕"。

| # | 阻力 | 性质 | 处理 |
|---|---|---|---|
| 1 | ~~**C5 的 WiFi 在本板从未验证**~~ | ✅ **已排除（2026-07-27 真机）** | 扫到 51 个 AP + ping 网关 5/5 0% 丢包 RTT 2~11ms，**证据见 §9.7**。两个原以为的坎也不成立：**C5 出厂自带 hosted 固件 ⇒ 不需要 USB-TTL**、`reset=36` 猜对了。剩一条**新的**：出厂 CP 是 2.12.9、与主机 3.0.5 major 不匹配 ⇒ 退 streaming 无 SW_AGGR，要经 SDIO 给 C5 做 OTA 才能吃到吞吐（§9.8 待办 1） |
| 1b | **本板是 v1.3 早期 P4 样片** | 本轮新发现，**新工程一律撞** | 新建工程默认 `REV_MIN_301`，烧录直接被拒；必须 `SELECTS_REV_LESS_V3` + `REV_MIN_100` 两行（少一行静默无效）。见 §9.6 |
| 2 | **C5 SDIO 与 microSD 争 SDMMC 控制器** | 配置问题 | ✅ **配置已解决**：`CONFIG_ESP_HOSTED_SDIO_SLOT_1=y`（C5 走 GPIO14-19，非 slot0 IOMUX），SD 卡留 slot0；两者同时用可参考 `mcu_hosted_sdio_sdmmc_combined` |
| 3 | **console 占着 GPIO37/38 = P4↔C5 的 UART** | 一行配置 | ✅ **已改**：`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`（日志仍走 COM7） |
| 3b | **ESP-IDF v5.5.4 的 SDIO 4092 字节发送上限** | 本轮新发现，**编译期硬失败** | ✅ **已打官方一行补丁**（`eh.py patch-idf`，改动已在 IDF master 上游）；细节与撤销命令见 §9.3 |
| 4 | **编码格式只能 MJPEG，不能 H.264** | 硬件限制，**必须遵守** | P4 只有硬件 H.264 **编码器**，解码是软件 tinyH264 ⇒ 收流显示走 **JPEG 硬解**；K230 侧输出 MJPEG |
| 5 | 带宽 | **不是阻力**（已算） | 可用 TCP≈44Mbps；1280×452 MJPEG@30fps 约 21Mbps（YUV420 868KB/帧、10:1 压缩估）、640×480@30fps 约 4Mbps ⇒ 余量充足 |
| 6 | P4 侧要写：socket 收流 + JPEG 硬解 + 送 LVGL | 常规开发 | IDF 有 `jpeg_decode` 例程 + `esp_lv_decoder`；LVGL 侧用 canvas/image |
| 7 | K230 侧要写：抓帧 + JPEG 编码 + socket 推流 | 常规开发 | CanMV(MicroPython) 有 WLAN 例程可打底 |
| 8 | 屏比例 **1280×452** 极端长条 | 体验问题 | 摄像头 4:3/16:9 画面贴上去会大量留白或裁切 |

**⚠️ 先想清值不值**：若目标只是"人能看到画面"，方案 A（K230 直推手机/PC 浏览器）几分钟就能出效果，且不受这块长条屏比例限制。**D 的真正价值是打通"C5 无线链路 + P4 硬件解码显示"这套技能栈**，不是画面本身。要练这套栈就做 D，要看画面就做 A。

---

## 九、方案 D 第一步：C5 WiFi（ESP-Hosted）—— ✅ **真机通过（2026-07-27）**

> 目标（验收判据）：**C5 能扫到 AP + P4 侧能 ping 通**。这是方案 B/D 的共同前置，也是整条链路里唯一有未知风险的一环。
> **两条都已达成，真机实测数据见 §9.7**：扫到 **51 个 AP**（含 5G）· ping 网关 **5/5、0% 丢包、RTT 2~11ms**。
> **两个原以为的拦路虎都不成立**：① **C5 出厂就带 ESP-Hosted 协处理器固件（2.12.9）** ⇒ **USB-TTL 根本不需要**；② `reset=36` 这个猜测值实测能正常复位 C5。
> **新暴露的真问题一个**：主机 3.0.5 与协处理器 2.12.9 **major 版本不匹配** ⇒ 退化到 streaming 模式（无 SW_AGGR）、且 **AP 不给 DHCP 时链路要靠静态 IP**（详见 §9.7 与 §9.8）。

### 9.1 三个工程（都在 `workbench/esp32p4/`，按 gitignore 不入库）

| 工程 | target | 角色 | 产物 | 需要凭据 |
|---|---|---|---|---|
| `c5_cp` | esp32c5 | 协处理器（射频侧）固件 = ESP-Hosted "cp" | `eh_cp_wifi_sta.bin` **1116 KB**（app 分区 1.75MB，余 38%） | 否 |
| `p4_scan_host` | esp32p4 | 主机 App：扫 AP | `scan.bin` **572 KB** | **否**（第一步就用它） |
| `p4_sta_host` | esp32p4 | 主机 App：连 AP + DHCP（为 ping） | `wifi_sta_mcu_host.bin` | **要 SSID/密码**（现仍是 `myssid`/`mypassword` 占位） |

一键重建：
```powershell
# 建工程（例程名见 9.2）
powershell -File tools\create_hosted_projects.ps1 -Feature "wifi/scan"
powershell -File tools\create_hosted_projects.ps1 -Feature "wifi/sta"
# set-target + 编译（三个工程都能单独指定）
powershell -File tools\build_hosted.ps1 -Project c5_cp
powershell -File tools\build_hosted.ps1 -Project p4_scan_host
# 上板 + 判读（板子插上后一条命令出 PASS/FAIL/INCONCLUSIVE）
powershell -File tools\hosted_bringup.ps1 -Project p4_scan_host
```

> ⚠️ 这三个工程**可以**跑 `idf.py set-target`（新建工程、没有需要保护的 sdkconfig）。
> §五 那条"永远不要 set-target"的禁忌**只针对 `p4_lcd`**（它的 sdkconfig 带防花屏关键项），别混。

### 9.2 例程名：`slave` 已经不存在了（踩过一次）

`esp_hosted` **2.x** 只有一个叫 `slave` 的例程；**3.0.0 起整树重构**成成对的 `<feature>/mcu_host` + `<feature>/cp`，`slave` 被删。所以：

```
idf.py create-project-from-example 'espressif/esp_hosted:slave'
  -> ERROR: Cannot find example "slave" for "espressif/esp_hosted" version "*"
```

**例程名/版本/target 的真值源 = 组件仓 API**，别靠记忆：
```powershell
curl https://components.espressif.com/api/components/espressif/esp_hosted
# .versions[] 里看 .version / .targets / .examples[].name
```
核到的事实：**3.0.5（2026-07-23 发布）要求 IDF ≥ 5.5、`.targets` 含 `esp32c5`；2.x 的 `.targets` 里没有 c5** ⇒ 这块板必须用 3.x。已在三个工程的 `main/idf_component.yml` 里把版本**钉死 `3.0.5`**（原本是 `'*'`，会随下次发版漂）。

**一个省一半工作量的发现**：`wifi/scan/cp` 与 `wifi/sta/cp` 逐文件比对，**只差一行注释和 project 名** ⇒ **C5 端固件只需编一份**（本仓库只保留 `c5_cp`，`wifi/scan/cp` 那份已删）。CP 端的功能全在组件里，例程的 `main.c` 只做 NVS + event loop。

### 9.3 必须打的 ESP-IDF 补丁（一行，官方脚本，已应用）

SDIO 协处理器默认走 **SW 聚合**（吞吐量卖点），而 IDF v5.5.4 的 `sdio_slave.c` 还有 4092 字节单笔发送上限 ⇒ **编译期直接 FATAL_ERROR**，不是警告：

```
This ESP-IDF still has the 4092-byte SDIO send cap
CMake Error ... SDIO SW_AGGR: this ESP-IDF lacks the send-cap fix
```

组件自带修法（改动已在 IDF master 上游）：
```powershell
D:\esp32\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe `
  d:\diansai\workbench\esp32p4\c5_cp\managed_components\espressif__esp_hosted\tools\eh.py `
  patch-idf --idf-path D:\esp32\Espressif\frameworks\esp-idf-v5.5.4
```
**改动就一行**（`components/esp_driver_sdio/src/sdio_slave.c`）：
```c
- SDIO_SLAVE_CHECK(len > 0 && len <= 4092, "length out of range: (0, 4092]", ESP_ERR_INVALID_ARG);
+ SDIO_SLAVE_CHECK(len > 0, "len <= 0", ESP_ERR_INVALID_ARG);
```
**已于 2026-07-27 应用到本机 IDF v5.5.4**（`patch successful`，`git diff` 可见）。影响面：只碰 SDIO **slave** 驱动；`p4_lcd` 用的是 SDMMC **host**（SD 卡），不受影响。
撤销：
```powershell
& "D:\esp32\Espressif\tools\idf-git\2.43.0\cmd\git.exe" -C D:\esp32\Espressif\frameworks\esp-idf-v5.5.4 `
  checkout -- components/esp_driver_sdio/src/sdio_slave.c
```
> 也可以改用 `CONFIG_EH_TRANSPORT_CP_SDIO_MODE_PACKET=y` 绕开补丁（不改 IDF、吞吐低些）。**没这么做**：bring-up 阶段要尽量待在官方默认上，出问题才好判"是板子还是我们改的配置"。

### 9.4 引脚与配置（三方交叉核对，比原理图文本提取可靠）

**C5 侧（协处理器）：SDIO slave 引脚是硬件固定的，不用配也不能改**

| 信号 | C5 GPIO | 三处独立来源 |
|---|---|---|
| CLK | **9** | IDF `soc/esp32c5/include/soc/sdio_slave_pins.h` · esp_hosted `Kconfig.cp.sdio` 的 `default ... if IDF_TARGET_ESP32C5` · 官方 `docs/getting-started-mcu.md` 协处理器连接表 |
| CMD | **10** | 同上 |
| D0 / D1 / D2 / D3 | **8 / 7 / 14 / 13** | 同上 |
| Reset In | C5 的 `RST`/`EN`（本板引到 6P 排针 `C5_EN`） | 官方表 |

**P4 侧（主机）：本板与官方 P4-Function-EV-Board 的"P4+C6"映射一致**

| 信号 | 本板 P4 GPIO | 说明 |
|---|---|---|
| CLK / CMD | **18 / 19** | 与 esp_hosted 内置 `P4X_C5_DEV_BOARD_FUNC_BOARD` 预设逐脚相同 |
| D0..D3 | **14 / 15 / 16 / 17** | 同上 |
| slot | **1**（可路由脚） | slot0 是 IOMUX 死脚 CLK43/CMD44/D0-3=39-42，**留给 microSD** ⇒ 两者可共存 |
| 总线宽度 / 时钟 | 4-bit / 40 MHz | 本板是 PCB 走线 + 51kΩ 上拉（原理图上 R3/R5/R6/R7/R13/R14），满足 4-bit 的硬性要求 |
| Slave Reset | **36**（`待确认`） | 见下 |

**四条必改的 sdkconfig（已写进两个 host 工程的 `sdkconfig.defaults`，带注释）**

1. `CONFIG_SLAVE_IDF_TARGET_ESP32C5=y` —— **最容易漏的一条**：`esp_wifi_remote` 在 P4 上把协处理器**默认成 C6**，实测生成的 sdkconfig 里就是 `CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6=y`。改这一行会连带把 `ESP_HOSTED_CP_TARGET_ESP32C5` 翻过来。
2. `CONFIG_ESP_HOSTED_HOST_SDIO_PIN_*` 六条显式写死 18/19/14/15/16/17。（board 选 `NONE` 时这六个默认值**恰好**就是这些，但显式写死才能挡住上游改默认、也顺便把本板接线记在配置里。）
3. `CONFIG_ESP_HOSTED_HOST_RESET_GPIO=36` + `ACTIVE_LOW=y` —— **`-1` 不是选项**：`eh_host_bus_sdio.c:1922` 有 `assert(reset_pin.pin != -1)`。36 是**尚未证实的最佳猜测**（原理图文本里 `C5_EN` 紧邻 P4 `IO36`，且 IO36 在本板没有别的功能），选它的安全性论证是：**猜错也无害**——IO36 不是背光(30)/触摸(27,28,29)/SDIO(14-19)/console(37,38)/LCD_RST(26)/SD(39-44) 里的任何一个；而 C5 的 EN 板上有上拉，主机不驱动它 C5 也在跑。
4. `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` —— 默认 UART0 console 就压在 GPIO37/38（= P4↔C5 的 UART）。改成 USB-Serial-JTAG 后日志仍从 COM7 出来（COM7 本来就是 `VID_303A&PID_1001` = USB-Serial-JTAG），同时把 37/38 彻底让开。§8.3 第 3 条至此**已处理**。

> 原理图 PDF 的文本提取（markitdown / pdfminer）在这种图纸上**不可靠**：文字块被旋转/镜像，`IO36` 之类的 token 只是"画在附近"，不代表连线。本节的引脚全部是**用三处独立来源交叉核对**得到的，而不是读一次 PDF 就写下来。可信度对照：LCD_BL=30、触摸 28/29/27 这两组已被真机实验证实，与同一列提取结果吻合 ⇒ 那一列（含 `IO32=C5_BOOT`）可信度较高；`C5_EN=IO36` 不在那一列，仍是猜测。

### 9.5 上板怎么做（**已按本节执行完，结果见 §9.7**）

> 结果速览：第一发就 PASS，`slave chip id` 直接出来 ⇒ **C5 出厂自带 hosted 固件，下面"判 FAIL 才需要 USB-TTL"那一段没走到**。本节保留作流程与备选留档。

**第一发先不碰 C5**（零成本、信息量最大）：直接给 P4 刷 `p4_scan_host`，看 C5 是不是**出厂就带**某版 ESP-Hosted 协处理器固件（这块板卖点就是 P4+C5 的 WiFi6，有可能自带）。
```powershell
powershell -File tools\hosted_bringup.ps1 -Project p4_scan_host
```
脚本会 flash → RTS 复位抓 25 秒日志 → 按下面的判据出结论：

| 日志标记 | 含义 | 判决 |
|---|---|---|
| `Total APs scanned = N`（N>0） | 链路通 + 协处理器真的扫到了 AP | **PASS**（验收判据的前半条达成） |
| `slave chip id: 0x..` / `capabilities: 0x..` | SDIO 通 + init-event 握手完成 | PARTIAL（还没扫到 AP） |
| 只有 `transport[host]: SDIO 4-bit 40000 kHz` | 主机自己起来了，**协处理器一声没出** | **FAIL** ⇒ 是接线/传输配置问题，**不要去调 WiFi**（官方 debugging order 第 3 步） |
| 连 `transport[host]` 都没有 | 刷的可能不是 host app | INCONCLUSIVE |

> ⚠️ 这一步会**覆盖 P4 上的 `p4_lcd` LVGL 面板固件**。恢复一条命令：`powershell -File tools\build_p4.ps1 -Flash`（出厂固件另有 2MB 备份）。

**若判 FAIL（= C5 没有 hosted 固件），才需要给 C5 刷 `c5_cp`：**
- 官方只给"CP 有自己的串口"这一条路 ⇒ 本板 C5 的 UART0 引到顶部 6P 排针 `PZ127-1-06-Z`（`3V3 / GND / C5_TXD0 / C5_RXD0 / C5_EN / C5_BOOT`）⇒ **需要一个 USB-TTL（3.3V）**：TTL 的 TXD→C5_RXD0、RXD→C5_TXD0、GND 共地；进下载模式 = C5_BOOT 拉低时给 C5_EN 一个低脉冲。
- 官方还提醒：**若因主机占着总线导致刷不进，先把 P4 弄进 bootloader**：
  `esptool.py -p COM7 --before default_reset --after no_reset run`
- ⬜ **没有 USB-TTL 时的备选（未做、评估级）**：用 `espressif/esp-serial-flasher` 让 P4 当烧写器（P4 UART1 走 GPIO37/38 → C5 UART0，再由 P4 控制 C5_EN/C5_BOOT）。原理成立，但要先确认 `C5_EN`/`C5_BOOT` 到底挂在 P4 哪两个脚（`C5_BOOT≈IO32` 可信度较高、`C5_EN≈IO36` 是猜测），属"几小时级"的活，不是一行配置。

### 9.6 上板前撞的一堵墙：**本板是 v1.3 早期片，新工程默认编不出能烧的固件**

第一次烧录直接被 esptool 拒了（**不是接线、不是端口**）：

```
Chip is ESP32-P4 (revision v1.3)
A fatal error occurred: bootloader/bootloader.bin requires chip revision in
range [v3.1 - v3.99] (this chip is revision v1.3). Use --force to flash anyway.
```

IDF v5.5.4 新建工程默认 `CONFIG_ESP32P4_REV_MIN_301`（v3.1+），本板是**早期 v1.3 样片**。

**⚠️ 别用 `--force`**：min-rev 是烧进镜像头的，而 IDF 自己的 Kconfig 写着"**Support of ESP32-P4 rev. <3.0 and >=3.0 is mutually exclusive**"——v1.x 与 v3.x 硬件差异大，不是"改个头"的事。

**正解两行，且顺序有讲究**（第一次我只加了后一行，白编一轮）：

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y   # 总闸门：不开它，REV_MIN_0/1/100 这几个选项根本不出现
CONFIG_ESP32P4_REV_MIN_100=y           # 实测：单独写它 -> sdkconfig 仍是 REV_MIN_FULL=301，静默无效
```

改完**必须重新编译**（不是重新烧）。改对后 sdkconfig 与真机 boot log 都能对上：

```
CONFIG_ESP32P4_REV_MIN_FULL=100 / MAX=199 / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360
boot: chip revision: v1.3   |   efuse_init: Min chip rev: v1.0  Max chip rev: v1.99
```

> `SELECTS_REV_LESS_V3` 不只管版本门限，它还切换这一代硅片的一串默认值（**默认 CPU 频率 360MHz**、PSRAM、PM 选项）——这也解释了为什么 `p4_lcd` 一直跑在 360MHz。
> **⇒ 这块板上任何新建的 P4 工程，第一件事就是补这两行。** `p4_lcd` 里本来就有（它是从厂家工程改的），所以之前没暴露。

### 9.7 真机实测（2026-07-27，两发全过）

**第一发 `p4_scan_host`（刻意先不碰 C5）—— PASS：**

```
I (133) eh_sdio: transport[host]: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17 RESET=36
W (135) eh_sdio: Reset co-processor using GPIO[36]
I (1791) eh_sdio: Card init success, TRANSPORT_RX_ACTIVE
I (1836) eh_sdio: SDIO Host operating in STREAMING MODE
I (1868) eh_init_evt: slave chip id: 0x17 (esp32c5)
I (1868) eh_init_evt: capabilities: 0x0d   ->  * WLAN over SDIO / HCI over SDIO / BLE only
I (1869) eh_init_evt: esp-hosted fw versions: host=3.0.5 coprocessor=2.12.9
E (1869) eh_init_evt: major version mismatch ??? OTA coprocessor from host
I (1869) eh_init_evt: CP without SDIO SW_AGGR; compatible streaming mode enabled
I (12813) scan: Total APs scanned = 51
```

**第二发 `p4_sta_host`（连 AP + ICMP）—— PASS：**

```
I (2158) wifi station: STA_CONNECTED (associated, waiting for DHCP)
I (14115) wifi station: sta mac d0:cf:13:e8:57:0c (esp_wifi_get_mac=ESP_OK)
I (14115) wifi station: netif mac d0:cf:13:e8:57:0c (esp_netif_get_mac=ESP_OK)
W (14116) wifi station: no DHCP lease after 12000 ms -> falling back to static 192.168.4.3
I (14129) wifi station: PING reply seq=1 from 192.168.4.1 bytes=64 ttl=128 time=11 ms
I (15120..18120)      : seq=2..5  time = 3 / 3 / 5 / 2 ms
I (19117) wifi station: PING SUMMARY tx=5 rx=5 loss=0% duration=24 ms -> PING_OK
```

| 事实 | 实测值 | 意义 |
|---|---|---|
| SDIO 链路 | 4-bit @40MHz，`Card init success`，1.79s 起来 | §9.4 那套引脚**全部正确** |
| 协处理器身份 | `chip id 0x17 (esp32c5)` | 板上真是 C5，且**出厂自带 hosted 固件** |
| **出厂 CP 固件版本** | **esp_hosted 2.12.9**（主机 3.0.5） | major 不匹配 ⇒ 退 streaming、无 SW_AGGR（见 §9.8） |
| `reset=36` | 日志 `Reset co-processor using GPIO[36]`，1.73s 后收到 slave init event | **IO36 就是 C5 的复位线**（时序吻合；未做"关掉 reset"的对照实验，故仍留一分保留） |
| 扫 AP | **51 个**，2.4G(ch1/5/9/11) + **5G(ch149)** 都有 | C5 双频射频正常工作 |
| 关联 | `set_config` 后约 100ms 进 STA_CONNECTED | 关联无问题 |
| **DHCP** | **12s 拿不到租约**（改静态 IP 后一切正常） | **是 AP 那头的事，不是 C5/SDIO**（见 §9.8） |
| **ICMP** | **5/5、0% 丢包、RTT 2/3/3/5/11 ms** | **验收判据"ping 通"达成**；也给了图传链路的首个延迟基线 |
| MAC | `d0:cf:13:e8:57:0c`，两个 API 都 `ESP_OK` | **推翻了我自己的假设**（见 §9.8） |

真机日志原件已入库留证：[`firmware/esp_hosted_c5/boot_log_scan_PASS_2026-07-27.txt`](firmware/esp_hosted_c5/boot_log_scan_PASS_2026-07-27.txt) · [`boot_log_sta_ping_PASS_2026-07-27.txt`](firmware/esp_hosted_c5/boot_log_sta_ping_PASS_2026-07-27.txt)。

### 9.8 剩下的问题 + 诚实边界

**⬜ 待办 1（唯一有技术含量的一条）：把 C5 升到 esp_hosted 3.0.5。** 现在 host 3.0.5 / CP 2.12.9 major 不匹配，代价有两条：① 退到 **streaming 模式、拿不到 SW_AGGR 的吞吐**（正是 §9.3 打补丁想要的那个特性——**现在那个补丁只对我们自己编的 `c5_cp` 有意义**）；② 组件自己在日志里建议 `OTA coprocessor from host`。**⇒ 走 `ota/coprocessor_ota` 例程从 P4 侧经 SDIO 给 C5 做 OTA，仍然不需要 USB-TTL**；我们已经编好的 `c5_cp`（1116KB）就是要刷的镜像。**风险**：得先确认出厂 C5 的分区表有没有第二个 OTA 槽；ESP-IDF OTA 是"写非活动槽、成功才切换"，没槽会报错而不是变砖——但仍属"动别人出厂固件"，做之前值得先把出厂 C5 固件读出来备份（要读就得有 USB-TTL 了）。

**⬜ 待办 2：DHCP 为什么不给租约。** 已知不是 C5/SDIO 的射频问题（静态 IP 下 ICMP 5/5 通）。
**🔄 2026-07-27 更正（原判断已被推翻）**：本条原写"最可能是车端 ESP-01S 的 SoftAP 没给 DHCP"。**错了** —— §10.4 的对照实验里 **K230 连同一只 ESP-01S，2 秒关联 + DHCP 正常拿到 `192.168.4.3`** ⇒ **ESP-01S 的 DHCP 服务器是好的，锅在 P4/esp_hosted 这侧**（很可能同属 host 3.0.5 / CP 2.12.9 的 RPC 缺口，见 §10.5）。⇒ **正确判法：等 C5 OTA 到 3.0.5 之后重测**，别再去查 AP。

**🔄 一次自我推翻（留档）**：看到扫描那轮日志里 `esp_wifi_get_mac failed with -1` + 两个 RPC 5s 超时，我推断"版本不匹配把 MAC 读挂 ⇒ netif 没有 MAC ⇒ DHCP 必然失败"。**为验证专门加了打印，实测 `esp_wifi_get_mac=ESP_OK`、MAC 正常 ⇒ 假设错了。** 教训：把假设做成一行打印比继续推理便宜得多（本仓库"AI 必须能自我推翻"那条的又一次实例）。

**其它诚实边界**：
- ✅ 真机已验：SDIO 链路 / C5 身份 / 扫 AP / 关联 / ICMP / reset 脚 / rev-min 两行的必要性 —— 都有日志。
- ⬜ **未验**：`c5_cp` 那 1116KB 固件**从未烧进 C5**（现在跑的是出厂 2.12.9）· 吞吐/带宽**一个数都没实测**（§八的 44Mbps 仍是官方数字，且当前 streaming 模式还够不上）· MJPEG 收流显示、K230 侧推流 **全部没开工**。
- ⚠️ **§9.3 的 IDF 补丁是"本机状态"、不随 git 走**（IDF 在仓库外）。换机/重装 IDF 后编 `c5_cp` 会重现那个编译期 FATAL，照 §9.3 再打一次即可——不是配置退化。
- ✅ 工具脚本现在**三个都端到端跑过**了（`hosted_bringup.ps1` 的 flash/抓日志/判读三段这轮首次真跑，并在两个工程上各出了一次正确判决）。

---

## 十、方案 D 第二步：K230 → P4 数据链路（2026-07-27～28 · **机器侧端到端已通过，屏幕观感待人眼确认**）

> 目标：让 K230 把 MJPEG 帧推到 P4。本节把这一步的**真机事实**和**结论性判断**都记下来；
> **当前状态：AP 关联通了、TCP 连不上，判定为"不该在版本不匹配的栈上继续调"，已备好 C5 OTA 工程等放行。**

### 10.1 K230 侧的硬事实（都是真机 REPL 读出来的，不是资料推断）

板子 = **庐山派 CanMV-K230**（`os.uname()`: `machine='k230_canmv_lckfb'`, `sysname='rt-smart'`, 固件 `v1.8-13-gbbc87b8` 编译于 2026-07-24），MicroPython **1.21.0**，串口 **COM3**（`VID_1209&PID_ABD1`）是 MicroPython REPL。

**`/dev` 一次列清所有能力**（比翻资料快得多，推荐上手新板子第一件事就干这个）：

| 设备节点 | 意味着 |
|---|---|
| `sta` `ap` `w0` `w1` `netmgmt` | **WiFi 硬件在，且 station / AP 两个角色都有**（K230 芯片本身无 WiFi，是板上模块） |
| **`sensor_gc2093_csi2`** | **摄像头在**：GC2093 挂 CSI2 |
| `venc_device` `vdec_device` | 硬件视频编 / 解码器都在 |
| `vo_device` `connector` `vg_lite` | 显示输出（板子自己那块屏） |

**JPEG 编码怎么拿**（方案 D 的关键，因为 P4 只能硬解 JPEG）：
- `image.Image` **没有任何 compress/jpeg 方法**（实测 `[m for m in dir(image.Image) if 'com' in m ...]` → `[]`）⇒ **不能靠 `img.compress()`**。
- **`media.vencoder.Encoder` 有 `PAYLOAD_TYPE_JPEG`** ⇒ 走硬件 VENC。取字节流的写法（抄 `02-Media/video_encoder.py`）：
  ```python
  encoder.GetStream(streamData)
  for i in range(streamData.pack_cnt):
      b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
  encoder.ReleaseStream(streamData)
  ```
  ⚠️ **SD 卡上 30 个例程目录里没有一个用过 `PAYLOAD_TYPE_JPEG`**（grep 过），所以 `ChnAttrStr(...)` 给 JPEG 时的 profile 参数**只能真机试**，别当已知。

**⭐ SD 卡上有完整 CanMV 例程树，是 API 的最佳真值源**（`/sdcard/examples/`，30 个目录）。本轮直接照抄了 `14-Socket/network_wlan_sta.py`（WLAN STA）、`14-Socket/tcp_client.py`（socket 正确写法）、`02-Media/video_encoder.py`（VENC）。**上手这块板先 `os.listdir('/sdcard/examples')`，别凭记忆写 API。**

### 10.2 P4 侧：SoftAP + MJPEG sink（已上板运行）

新工程 `p4_softap_host`（源码备份见 `firmware/esp_hosted_c5/p4_softap_host.*`）：

- **P4 当 AP**（不是 STA）：理由是 esp_hosted 的 SoftAP 自带 DHCP 服务器，而 K230 侧只需要走它最常见的 STA 连接路径。
- **AP 地址刻意改成 `192.168.7.1`**：车上那只 ESP-01S 的 SoftAP 也发 `192.168.4.0/24` + 网关 `192.168.4.1` ⇒ 两个 AP 同网段时，客户端的 IP 完全无法证明它连的是谁（本轮被这一点坑过，详见 10.3）。换网段后 **客户端 IP 本身就是证据**。
- **MJPEG sink**：tcp/5000，帧格式 `'J' 'F' | uint32 LE 长度 | JPEG`，每秒打一行 `SINK ... fps | Mbps | frame min..max`，并检查 `FFD8..FFD9` 标记。目的是**在这块板上实测吞吐**（README §八那个 44Mbps 是官方数字，本板从未测过）。
- 真机启动日志：`AP addr 192.168.7.1` + `SINK listening on tcp/5000` ✅

### 10.3 一路撞过来的六个坑（每个都花了真金白银）

1. **⭐ ESP SoftAP 默认要求 PMF，第三方 station 关联不上** —— 上游 softap 例程写死 `.pmf_cfg.required = true`。改成 `required=false` **还不够**，IDF 的 `.capable` 默认仍是 `true`；**两个都关掉**之后 K230 才关联上（现象：AP 在 K230 的扫描里 **-18dBm、security 正是 `SECURITY_WPA2_AES_PSK`**，却一直 association timeout）。
2. **K230 的 MicroPython REPL 反复 paste 会耗尽堆** —— 表现是 `socket.connect()` 报 `OSError(12)`，误导成 socket 问题；**判据是连 `open(小文件).read()` 都报同一个 12(ENOMEM)**。修法：`gc.collect()` + 工具加软复位（`k230_repl.ps1 -SoftReset`，实测复位后 `mem_free≈4.1MB`）。
3. **CanMV 的 socket 要用官方写法** —— `socket.socket(AF_INET, SOCK_STREAM, 0)`（proto 显式给 0）+ **`getaddrinfo()` 取地址**；直接 `connect(("ip", port))` 会失败。
4. **`machine.reset()` 会让 USB CDC 重新枚举** ⇒ 已打开的串口句柄失效，之后发的东西**静默丢失**（第一次因此整轮跑空）。工具已改成"发复位 → 关口 → 等端口消失再出现 → 重开"（`-HardReset`）。
5. **两个 AP 同网段 ⇒ "连的是谁"不可判**（见 10.2）。另外 **RT-Smart 的网络栈在 MicroPython 软复位后不重置**（`Network (rt-smart) is always active and cannot be disabled`）⇒ 旧关联/旧租约/旧 ARP 会跨实验存活。
6. **Windows GBK 控制台 + 工具链打 emoji ⇒ 伪装成编译错误** —— 编 OTA 工程时崩在 `UnicodeEncodeError: 'gbk' codec can't encode character '\U0001f50d'` + `RuntimeError: Event loop is closed`。修法已收进 `tools/idf_shell.ps1`：`PYTHONIOENCODING=utf-8` + `PYTHONUTF8=1`。

### 10.4 关键对照实验：问题不在 K230，也不在 ESP-01S 的 DHCP

K230 连**已知能用的** `DIANSAI_CAR`（车上 ESP-01S）：**2 秒关联成功 + DHCP 拿到 `192.168.4.3`**（证据 `firmware/esp_hosted_c5/k230_control_assoc_to_esp01s_PASS.txt`）。

⇒ 两个结论：
- **K230 的 STA 完全正常**，问题特定于"K230 ↔ C5 SoftAP"这一对；
- **🔄 自我推翻上一轮的判断**：§9.8 待办 2 曾写"P4 当 STA 拿不到租约 = ESP-01S 那头没 DHCP"。**错了** —— ESP-01S 的 DHCP 服务器好得很，是 P4/esp_hosted 这侧的问题。§9.8 那条已按此更正。

### 10.5 为什么停手：**不该在已知版本不匹配的栈上调互操作**

K230 关联上 P4_STREAM 之后，`TCP connect → OSError(107) ENOTCONN` 稳定复现（换了官方 socket 写法、换了网段、清了堆都一样）。于是在**我完全控制的 P4 侧装仪表**（而不是继续在 K230 侧试错），拿到两个决定性读数：

| 仪表读数 | 判读 |
|---|---|
| `esp_wifi_ap_get_sta_list()` 返回 **10 个全零 MAC** | 这个 API 在当前 CP 上**返回垃圾** ⇒ 仪表本身不可信 |
| 每次 K230 尝试连接，AP 侧成对出现 `wifi event id=43` | 数 IDF v5.5.4 的 `wifi_event_t` 枚举：**43 = `WIFI_EVENT_HOME_CHANNEL_CHANGE`**（不是 station 事件；`AP_STACONNECTED`=14 / `AP_STADISCONNECTED`=15 从未出现） |

⇒ 加上启动就在报的 `major version mismatch ??? OTA coprocessor from host`（host **3.0.5** vs 出厂 CP **2.12.9**），**判定：先把栈修正，再谈互操作**。继续在这上面调，等于在不可信反馈上调控制器——本仓库最贵的那类错误。

> 附：AP 事件 id 在 esp_hosted 的 RPC 转发后**不能用名字常量匹配**（上游例程的 `event_id == WIFI_EVENT_AP_STACONNECTED` 分支从未命中）。要看事件就**先把 id 全打出来，再去数枚举**。

### 10.6 已备好、**等放行**：给 C5 做 SDIO OTA 到 3.0.5

新工程 `p4_cp_ota`（编译通过：`host_performs_slave_ota.bin` **457KB**；构建期已确认 `Partition OTA: Found slave_fw partition at offset: 0x5F0000`）：

- 走 **`CONFIG_OTA_METHOD_PARTITION`**：把协处理器镜像放进主机 flash 的 `slave_fw` 分区，再经 RPC 推给 C5。**刻意不用 HTTPS 方式** —— WiFi 正是要修的东西，升级通路不能依赖它。
- 镜像 = 我们已编好的 `c5_cp/build/eh_cp_wifi_sta.bin`（1116KB），已放进 `components/ota_partition/slave_fw_bin/`。
- 主机分区表用例程自带的 8MB 布局（本板 flash 16MB，够）。

**⛔ 还没烧，因为它一烧就动手**：`app_main` 里没有命令行门控，开机直接 `Starting slave OTA update...` → `eh_host_cp_ota_activate()` → C5 重启。

**风险交代（要你拍板）**：ESP-IDF 的 OTA 写的是**非活动槽、成功才切换**，理论上失败会报错而不是变砖；但**出厂 C5 的分区表我们没读过**（不知道有没有第二个 OTA 槽），而 C5 一旦起不来，救它需要 **USB-TTL 接 6P 排针**——那是我们至今没用上、也不确定你有没有的东西。
**更稳的顺序**：先用 USB-TTL 把出厂 C5 固件**读出来备份**，再做 OTA。要是你没有 USB-TTL，就是"不备份直接升"，我建议你明确同意再做。

一条命令即可执行（放行后）：
```powershell
powershell -File tools\build_hosted.ps1 -Project p4_cp_ota -Flash -Port COM7
powershell -File tools\p4_boot_read.ps1 -Port COM7 -Seconds 60   # 看 OTA 进度与结果
```

### 10.7 本节诚实边界

- ✅ 真机已验：K230 身份/外设清单/WiFi 与摄像头存在性 · JPEG 编码器可用性（**API 存在**，未跑通编码）· P4 SoftAP + sink 上板运行 · PMF 是关联失败主因 · K230 STA 对照实验 · 事件 id 解码 · `sta_list` API 返回垃圾。
- ⬜ **未通**：K230 → P4 的 **TCP 一次都没连上**，所以**吞吐一个数都没有**；摄像头**一帧都没抓过**；MJPEG 编码**没跑过**；P4 侧 JPEG 硬解 + 上屏**没开工**。
- ⬜ `p4_cp_ota` **只编译过、未烧录**（烧=动手，见 10.6）。
- ⚠️ **本轮写的代码里有几段"从未被执行过"，下次别当已验证**：
  - `p4_softap_host` 的 **sink 收帧路径**（`read_exact` / `'JF'` 帧头解析 / `FFD8..FFD9` 校验 / `SINK ... fps | Mbps` 统计）—— **一个字节都没收到过**，只验证了"能起来、能 listen"。
  - `k230_link_test.py` 的 **发送循环与吞吐计算** —— 卡在 TCP connect 之前，**一次没跑到**。
  - `sta_watch_task` —— 跑过，但依赖的 `esp_wifi_ap_get_sta_list()` 在当前 CP 上**返回垃圾**，所以它现在是个**已知不可信的仪表**（升级 CP 后再看它是否变可用）。
  - `k230_repl.ps1 -HardReset` 的"等端口先消失"分支 —— 实测那次打印 `disappeared=False`（复位太快没抓到消失），**该分支未被真正走到**。
- ⚠️ `k230_link_test.py` 里的 AP 口令是明文占位（归档副本已抹）；真值只在一处，别复制。
- ⚠️ **"不需要 USB-TTL"这句要限定范围**：§九 的结论（扫 AP + ping）确实不需要它，因为 C5 出厂自带固件。但**给 C5 做 OTA 之前想先备份出厂固件，就必须有 USB-TTL**（读 C5 的 flash 只能走它的 UART）。两处别混着读。

### 10.8 ✅ K230 采集链路真机通过：硬件 JPEG **57 fps**（2026-07-27，无网络参与）

链路的网络那半卡住时，先把**不依赖网络的那半**做完 —— 而且它恰好包含全链路唯一真正未知的 API。

**做法（刻意隔离变量）**：`k230_jpeg_test.py` 只做 摄像头 → 硬件 VENC(JPEG) → 数字，**完全不碰网络**。这样万一失败，锅只能是相机/编码器，与那个卡住的 TCP 问题不混在一起。

**实测（一次通过）**：

```
JPEG mem_free=3990912
JPEG sensor configured 640x480 YUV420SP
JPEG ChnAttrStr accepted profile=2          <- 全仓库唯一没人验过的 API, 现在有答案
JPEG pipeline running, grabbing 60 frames
JPEG 60 frames in 1053 ms -> 56.98 fps
JPEG frame bytes min=6608 avg=7111 max=7239
JPEG first-frame markers FFD8..FFD9 = True (len=6625)
JPEG implied bitrate at measured fps = 3.24 Mbps
JPEG wrote first frame to /sdcard/jpeg_test.jpg
JPEG RESULT: PASS
```

**又把那一帧传回 PC 独立验证**（base64 过 REPL → 6625 字节，与板上报的一致 → `System.Drawing` 解码）：
**`DECODED OK -> 640x480`**，`FFD8`开头 `FFD9`结尾，是一张**真实可解码的照片**（画面偏暗、左侧一道亮边 —— 镜头当时对着暗处）。
证据入库：[`firmware/esp_hosted_c5/k230_jpeg_frame1_PASS_2026-07-27.jpg`](firmware/esp_hosted_c5/k230_jpeg_frame1_PASS_2026-07-27.jpg) + [`k230_jpeg_encode_PASS_2026-07-27.txt`](firmware/esp_hosted_c5/k230_jpeg_encode_PASS_2026-07-27.txt)。

**已确定的 API（照抄即可，别再试）**：

```python
width = ALIGN_UP(640, 16)
sensor = Sensor(); sensor.reset()
sensor.set_framesize(width=width, height=480, alignment=12)
sensor.set_pixformat(Sensor.YUV420SP)
encoder = Encoder(); encoder.SetOutBufs(8, width, 480)
chnAttr = ChnAttrStr(encoder.PAYLOAD_TYPE_JPEG, encoder.H264_PROFILE_MAIN, width, 480)  # profile=2, JPEG 下无意义但必须给
encoder.Create(chnAttr)
link = MediaManager.link(sensor.bind_info()['src'], (VIDEO_ENCODE_MOD_ID, VENC_DEV_ID, encoder.chn))
encoder.Start(); sensor.run()
# 取帧：GetStream -> 遍历 pack_cnt 用 uctypes.bytearray_at 拿字节 -> ReleaseStream
```
⚠️ `bytearray_at()` 给的是**指向编码器缓冲的视图**，`ReleaseStream()` 之后就失效 —— 要留着用必须先拷（本脚本里 `bytes(b)`）。

**对方案 D 的影响（下调了带宽预算）**：

| 项 | README §八 原先的纸面估算 | 本轮实测 |
|---|---|---|
| 分辨率 | 1280×452（贴屏比例） | 640×480 |
| 帧率 | 30 fps（假设） | **57 fps 实测**（硬件编码器余量很大） |
| 单帧大小 | ~87KB（按 10:1 压缩推） | **6.6~7.2KB** |
| 需要的码率 | ~21 Mbps | **3.24 Mbps @57fps ≈ 1.7 Mbps @30fps** |

⚠️ **诚实边界：7KB/帧是下限，不是代表值** —— 当时画面很暗、细节极少，JPEG 自然很小。明亮复杂场景同参数下可能是 20~60KB/帧（即 30fps 下 5~15 Mbps）。**但即便如此，也远低于这条链路的能力**（官方 SDIO 4-bit 端到端 TCP 44 Mbps；就算当前退化的 streaming 模式只有零头也够）⇒ **带宽从头到尾都不是方案 D 的瓶颈，链路能不能通才是。**

**仍未做**：这些帧**一帧都没送上网**（TCP 那半见 §10.5）；P4 侧 JPEG 硬解 + 上屏没开工。

### 10.9 ✅ P4 侧硬件 JPEG 解码 **3.31 ms/帧**（2026-07-27，同样不经网络）

把 §10.8 里 K230 真实产出的那一帧**嵌进屏工程固件**（`main/k230_frame1.jpg` + `EMBED_FILES`），用 P4 的硬件 JPEG 解码器解出来送 LVGL 上屏。**刻意不经网络** —— 网络那一跳另有阻塞，摘掉它才是单变量；把数据源从"嵌入数组"换成"socket 缓冲"是两行改动，其余（解码引擎/缓冲/LVGL 描述符）都不用变。

新增 `p4_lcd/main/jpeg_view.c/.h`（源码备份见 `firmware/esp_hosted_c5/`），真机日志：

```
jpeg_view: embedded frame: 6625 bytes, first2=FFD8 last2=FFD9
jpeg_view: header says 640x480 sampling=50331650
jpeg_view: decoded 614400 B in 3310 us | green avg 8/63 | nonzero 43886/43886 | raw 32..65535
jpeg_view: JPEG_VIEW RESULT: PASS 640x480 6625B->614400B 3.31ms
```

| 读数 | 判读 |
|---|---|
| `614400 B` 输出 | = 640×480×2 ⇒ **RGB565 输出正确** |
| **3.31 ms/帧** | ⇒ **约 302 fps 的解码能力**，30fps 只用掉 10% |
| green avg **8**/63 · 采样点 **100% 非零** · 原始值 **32..65535** | **输出是真图像、不是一片零**（"decode 返回 ESP_OK" 骗过我们不止一次，所以专门算了这几个数） |

**API 要点（IDF v5.5.4 `driver/jpeg_decode.h`）**：
```c
jpeg_decoder_get_info(bitstream, size, &info);              // 先拿宽高，再按需分配
jpeg_new_decoder_engine(&(jpeg_decode_engine_cfg_t){ .timeout_ms = 40 }, &dec);
// 输入/输出缓冲都必须用官方 helper 分配（DMA 能力 + 对齐），普通 malloc 或直接指 flash 常量都不行
in  = jpeg_alloc_decoder_mem(size,   &(jpeg_decode_memory_alloc_cfg_t){ JPEG_DEC_ALLOC_INPUT_BUFFER  }, &in_alloc);
out = jpeg_alloc_decoder_mem(w*h*2,  &(jpeg_decode_memory_alloc_cfg_t){ JPEG_DEC_ALLOC_OUTPUT_BUFFER }, &out_alloc);
memcpy(in, embedded_jpg, size);                            // 拷进 DMA 缓冲
jpeg_decoder_process(dec, &cfg, in, size, out, out_alloc, &out_len);
// cfg: .output_format = JPEG_DECODE_OUT_FORMAT_RGB565, .rgb_order = ..._BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601
```
LVGL v9 侧：填 `lv_image_dsc_t`（`header.magic = LV_IMAGE_HEADER_MAGIC` / `cf = LV_COLOR_FORMAT_RGB565` / `stride = w*2`），再 `lv_image_create` + `lv_image_set_src`。

**两个真实的坑**：
1. **`k230_jpg_end[-2]` 编不过** —— 嵌入符号声明成 `const uint8_t []`，GCC 认为是数组、`-Werror=array-bounds` 直接拒（`array subscript -2 is below array bounds`）。改成先取运行时指针 `const uint8_t *p = start;` 再 `p[size-2]`。
2. **烧录后首次启动可能在 `MSPI Timing: Enter psram timing tuning` 处 panic 循环**（`Store/AMO access fault`）。**别急着怀疑自己刚写的代码** —— 那是 app_main 之前的 PSRAM 时序整定阶段。**连续复位 4 次实测：0 panic、每次都跑到 `jpeg_view` 的 4 行日志** ⇒ 判为烧录后首启的瞬态（这块是 v1.3 早期片 + PSRAM HEX 200MHz 的激进配置）。**判据：让它多复位几次，看是不是确定性的**，比读栈快得多。

**⇒ 方案 D 的两端算力都测完了，都不是瓶颈**：

| 环节 | 实测 | 30fps 需求 | 余量 |
|---|---|---|---|
| K230 硬件 JPEG **编码** | 56.98 fps | 30 | 1.9× |
| P4 硬件 JPEG **解码** | 3.31 ms/帧 ≈ 302 fps | 30 | 10× |
| 码率 | 1.7~3.2 Mbps（暗场景下限） | — | 链路能力的零头 |

**⬜ 还差的唯一一环就是网络那一跳**（§10.5，等 C5 OTA）。
**⬜ 上屏效果待人眼确认**：屏上应出现 `JPEG HW DECODE: PASS 640x480 ...` 绿字 + 右侧一张照片（放在 x=620，屏高 452 < 图高 480，**底部约 28 行会被裁掉**属预期）。若照片红蓝互换，把 `jpeg_view.c` 里 `.rgb_order` 从 `_BGR` 改 `_RGB` 即可（一行）。

### 10.10 🎉 方案 D 数据通路打通：**真相机视频 1025 帧过无线，零坏帧**（2026-07-27）

**结论先行：把"谁当 AP"翻过来就通了 —— 不需要先给 C5 做 OTA。**

| 拓扑 | 结果 |
|---|---|
| A：P4/C5 当 **AP** + K230 当 STA 主动连 | ❌ 关联时好时坏、TCP `ENOTCONN`（见 §10.5） |
| B：P4/C5 当 AP，**P4 主动外连** K230 的 server | ❌ 关联本身就上不去，没走到 TCP |
| **C：K230 当 AP + server，P4 当 STA + client** | ✅ **全通** |

**为什么翻转有效（有实测证据，不是运气）**：P4 自己的日志显示，它当 AP 时每 **~15.58 s** 成对出现 `WIFI_EVENT_HOME_CHANNEL_CHANGE`（实测间隔 15584 / 15580 / 15585 / 15585 ms），**这个 AP 在周期性离开自己的信道**，而我们没让它这么干 —— station 想关联时它常常不在。而 **P4 当 STA 的路径早就被证明过**（§9.7 关联 ESP-01S 的 AP + ping 5/5）。⇒ 把不可靠的那一侧从关键路径上拿掉。

**拓扑 C 的三步实测**：

```
① K230 起 AP（k230_ap_up.py）
   AP info -> ssid=K230_AP bssid=A4:E8:8D:1A:B8:90 channel=6 security=SECURITY_WPA2_AES_PSK band=2.4G
   AP ifconfig -> ('192.168.169.1', '255.255.255.0', '192.168.169.1', '192.168.169.1')

② P4 当 STA 关联 + ping（p4_sta_host）
   got ip:192.168.169.2 gw:192.168.169.1 mask:255.255.255.0     <- K230 的 AP 真的发了 DHCP 租约
   PING SUMMARY tx=5 rx=5 loss=0% duration=14 ms -> PING_OK      <- 且在 K230 自己的网段, 无歧义

③ 真相机视频（k230_ap_stream.py 推 / p4_sta_host 的 video_client 收）
   K230 发: STREAM sent 1025 frames 7759967 bytes in 20009 ms -> 51.23 fps  3.10 Mbps   RESULT: PASS
   P4  收: VIDEO first frame 6616 B jpeg_markers=OK
           VIDEO 57 fps | 3.33 Mbps | frame 7283..7322 B | total 977
           VIDEO link closed after 1025 frames (bad=0)           <- 1025 发 1025 收, 零坏帧
```

**⇒ 这条链路现在是真的**：K230 摄像头 → 硬件 JPEG 编码 → WiFi(K230 AP) → C5 → SDIO → P4 → `'JF'`+长度分帧解析 → 校验 `FFD8..FFD9`。前一轮更长的一次跑到 **1050 帧同样零坏帧**。

**一个重要的数据修正**：这次看到单帧最大 **58315 B**（画面变化剧烈时），坐实了 §10.8 那句"7KB/帧是下限不是代表值"。⇒ **接收缓冲不能按 7KB 设计**；现在按 64KB 分配（并做了 64→32→16KB 的降级，见下）。

**四个坑（都花了真金白银，已入坑库）**：
1. **⭐ 我的抓日志工具默认会 RTS 复位板子，把正在传输的连接掐了** —— 第一次跑时 K230 明明报 `sent 374 frames ... 33.34 fps`、P4 侧却只有一串 `connect failed errno=104`。原因是我在流传输中途用默认参数抓 P4 日志 ⇒ 板子重启、连接被 reset。**测量工具本身改变了被测系统**。修法：观察正在运行的系统一律 `-NoReset`。
2. **码率公式错 100 倍，靠"两端各自独立测同一量"才发现** —— P4 侧打出 `440 Mbps`（这条链路物理上不可能），而 K230 侧说 3.12 Mbps。`bytes*800/ms/10` 应为 `bytes*8/(ms*10)`。**⇒ 关键指标要有两个独立来源；只有一个数时，先问"它物理上可能吗"。**
3. **MicroPython 的监听 socket 默认非阻塞** —— 裸 `srv.accept()` 立刻抛 `OSError(11)/EAGAIN` 而不是等。修法：`srv.setblocking(True)` + 循环捕获 EAGAIN 重试（带总超时）。
4. **`malloc(128KB)` 在这个工程里失败** —— 它没开 PSRAM，WiFi+lwIP 已吃掉大部分内部堆。修法：**64→32→16KB 逐级降级并打印实际拿到多少**（`VIDEO frame buffer 65536 B (free heap 242560)`），而不是为一个远大于载荷的缓冲直接放弃。

**✅ 最后一段（收帧+解码+上屏合成固件）已完成机器侧端到端真机验证 —— 见 §10.11。**
**⬜ C5 OTA 已不再是前置**（可选性能项：升到 3.0.5 才能吃到 SW_AGGR 吞吐，并修掉 §10.5 那些 RPC 返回垃圾的问题；刷前仍建议先备份出厂 C5 固件）。

### 10.11 合并固件：收帧+解码+上屏进同一个工程（机器侧端到端✅ · **屏幕观感待人眼确认**）

**做了什么**：把 `p4_sta_host` 的接收循环搬进 `p4_lcd`，帧数据源从「固件里嵌的那一张」换成 socket 连续帧。合并固件保持 **K230=AP+TCP server、P4=STA+client**，数据路径为 K230 相机→硬件 JPEG→WiFi→ESP32-C5→SDIO→ESP32-P4→硬件 JPEG 解码→LVGL canvas。

| 项 | 状态 |
|---|---|
| 代码合并 | ✅ 新增 `main/video_stream.{c,h}` + `main/Kconfig.projbuild`，改 `ai_panel.c` / `main.c` / `sdcard.h` / `main/CMakeLists.txt` / `main/idf_component.yml` / `sdkconfig.defaults` |
| 编译 | ✅ 一次过、零 warning。`p4_mipi_lcd.bin` = 0x124080 B，8M app 分区还剩 86% |
| 烧录 | ✅ `Hash of data verified` ×3、`FLASH_EXIT=0` |
| 独立 USB 冷启动 | ✅ P4=`COM7`、K230=`COM3`；30 s 启动窗口内 SDIO 4-bit/40 MHz、C5 chip id `0x17`、P4=`192.168.169.2`、ping 3/3 零丢包、嵌入帧硬解 PASS、LVGL 61.5～63.0 FPS |
| **机器侧端到端长跑** | **✅ 180.024 s：K230 发送 3042 帧 / 56,299,775 B，P4 对账接收 3042/3042，`bad=0`；16.90 FPS、2.50 Mbps** |
| **屏幕观感** | **⬜ 待人眼确认**：右半边是否持续实时变化、颜色是否正常、方向是否正确。AI 无法仅凭串口证明这三项 |

#### 10.11.1 真实长跑证据与帧数对账

- K230 原始汇总：`VIEW sent 3042 frames 56299775 bytes in 180024 ms -> 16.90 fps  2.50 Mbps`，随后 `VIEW RESULT: PASS`。
- P4 长窗口最后覆盖到 `total 3012 bad 0`；该采集窗比 K230 推流结束早约 1.8 s，因此不能把少看到的 30 帧当丢帧。
- 不复位 P4 后补跑短流，**短流第一帧的累计值直接从 `total 3043 bad 0` 开始**。计数器每成功接收并解码一帧才加一，因此前一轮结束值必为 3042，和 K230 的 3042 帧逐帧闭合：**3042/3042、`bad=0`**。
- P4 长跑日志中端到端速率稳定在约 16.4～17.2 FPS、2.24～3.21 Mbps；硬解多数约 3.3～4.6 ms，短跑见约 6.1 ms。**真实合并性能按 16.90 FPS 收口**，不拿独立链路的约 51 FPS或独立解码的约 302 FPS冒充端到端帧率。
- 对固件日志行做锚定扫描：`decode failed=0`、`lvgl lock timeout=0`、`panic=0`、`assert=0`、`brownout=0`、`link closed=0`。K230 服务尚未启动时的 `video: connect failed errno=104, retrying` 是设计内的重连现象，不是长跑故障。
- 可追溯证据：[`firmware/esp_hosted_c5/wireless_display_PASS_2026-07-28.txt`](firmware/esp_hosted_c5/wireless_display_PASS_2026-07-28.txt)。该文件只摘录原始关键行、锚定扫描结果与尚未完成的人眼确认项。

#### 10.11.2 独立 USB A/B 结论

同一套固件、拓扑和负载下，原供电路径曾让 `COM7` 整个消失且 60 s 不重新枚举；**把 P4 单独接到另一电脑 USB 口后，`COM7` 在 30 s 启动、180 s 长跑和后续短跑中始终稳定**，同时没有 brownout/panic/link drop。由此已能确认：先前阻塞属于**供电路径/端口共享触发的系统级问题，而不是必须修改合并固件才能恢复的软件故障**。

诚实边界：没有示波器/电流表数据，无法继续区分「hub 过流保护、线损压降、接口接触」中的哪一个是最终电气根因；文档只写到 A/B 实验能证明的层级。当前定版操作是 **P4 独占 USB 供电，K230 使用另一 USB 口**。

#### 10.11.3 当前性能边界与可选优化

当前接收循环是同步路径：`recv` → `decode_and_show()` → `lvgl_port_lock()` → canvas 全帧刷新；这会把显示侧等待经 TCP 反压传回 K230。单项的 51 FPS 网络链路与 302 FPS 解码能力都不代表合并固件能达到同样帧率，**现状约 17 FPS 是已稳定验证的真实结果，不是 30/50 FPS**。

题目没有更高帧率硬要求时，不为数字大改显示栈。若后续确需优化，第一优先级是把网络接收与显示解耦，采用「接收任务只保留 latest frame、显示任务按自身节奏取最新帧、旧帧主动丢弃」；再用分阶段时间戳确认瓶颈，禁止先盲调 JPEG 质量或 WiFi 参数。

**合并时的设计选择（均已写入源码注释）**：

1. **画面区用 `lv_canvas` 而不是 `lv_image`**。`lv_canvas_set_buffer()` 内部自行处理图像缓存失效，不在应用层复刻缓存协议。
2. **双缓冲 + `lvgl_port_lock` 换手**。解码在锁外写另一块缓冲，锁内只换手；拿不到锁就跳过本帧上屏，不改动 LVGL 正在读取的缓冲。
3. **解码引擎建一次复用**，避免逐帧创建/销毁。
4. **永不放弃重连**：K230 可后开机或中途重启，P4 屏固件持续等待，不要求人为重启 P4。
5. **microSD 暂关**（`P4_ENABLE_SDCARD 0`）：卡走 SDMMC slot0、C5 走 slot1，两者在本板上的同时使用仍未验证，后续必须单变量测试。

**本轮唯一未闭合项（必须由人眼回答）**：屏幕右半边是否实时变化、颜色是否正常、方向是否正确。在得到观察结果前，这三项保持 `待人眼确认`，不写成 PASS。
