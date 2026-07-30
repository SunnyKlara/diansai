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

## 十、方案 D 第二步：K230 → P4 数据链路（2026-07-27～28 · **机器侧端到端 PASS；屏幕观感人眼 PASS（含中线修复）；剩「画面区轻微闪烁」待定因**）

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
**该静态单帧上屏判读已被实时视频结果取代（2026-07-28）**：§10.11/§10.12 的连续视频已由人眼确认颜色正常（无红蓝互换）、方向正确、持续实时变化 ⇒ 上屏链路成立，本项不再单独等判读。（历史说明：屏上应出现 `JPEG HW DECODE: PASS 640x480 ...` 绿字 + 右侧一张照片，放在 x=620、屏高 452 < 图高 480，**底部约 28 行被裁掉属预期**；若红蓝互换就把 `jpeg_view.c` 的 `.rgb_order` 由 `_BGR` 改 `_RGB`，一行。）

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

### 10.11 合并固件：收帧+解码+上屏进同一个工程（机器侧端到端✅ · **屏幕观感人眼✅**）

**做了什么**：把 `p4_sta_host` 的接收循环搬进 `p4_lcd`，帧数据源从「固件里嵌的那一张」换成 socket 连续帧。合并固件保持 **K230=AP+TCP server、P4=STA+client**，数据路径为 K230 相机→硬件 JPEG→WiFi→ESP32-C5→SDIO→ESP32-P4→硬件 JPEG 解码→LVGL canvas。

| 项 | 状态 |
|---|---|
| 代码合并 | ✅ 新增 `main/video_stream.{c,h}` + `main/Kconfig.projbuild`，改 `ai_panel.c` / `main.c` / `sdcard.h` / `main/CMakeLists.txt` / `main/idf_component.yml` / `sdkconfig.defaults` |
| 编译 | ✅ 一次过、零 warning。`p4_mipi_lcd.bin` = 0x124080 B，8M app 分区还剩 86% |
| 烧录 | ✅ `Hash of data verified` ×3、`FLASH_EXIT=0` |
| 独立 USB 冷启动 | ✅ P4=`COM7`、K230=`COM3`；30 s 启动窗口内 SDIO 4-bit/40 MHz、C5 chip id `0x17`、P4=`192.168.169.2`、ping 3/3 零丢包、嵌入帧硬解 PASS、LVGL 61.5～63.0 FPS |
| **机器侧端到端长跑** | **✅ 180.024 s：K230 发送 3042 帧 / 56,299,775 B，P4 对账接收 3042/3042，`bad=0`；16.90 FPS、2.50 Mbps** |
| **屏幕观感** | **✅ 人眼确认（2026-07-28）**：右半边持续实时变化、颜色正常、方向正确。AI 无法仅凭串口证明这三项，故必须由人判读 |

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

**本轮人眼结果（2026-07-28）**：右半边持续实时变化、颜色正常、方向正确三项均通过；当时另发现屏幕中间有固定线、运动的手跨线时出现前后帧重叠 —— **该缺陷已在 §10.12 末用「`buffer_px` 扩为整屏」单变量实验修掉并经人眼复验通过**，此处保留原始现象作为排查起点，别当现存缺陷。

### 10.12 ✅ latest-frame 解耦流水线：接收 56.62 FPS，显示约 16.5 FPS（2026-07-28）

> **验证边界**：机器侧端到端 180 秒长跑 **PASS**；人眼确认实时变化、颜色、方向 **PASS**；曾存在的固定中线与跨线重叠已由「`buffer_px` 扩为整屏」单变量实验修掉并经人眼复验 **PASS**（详见本节末）。**仍未闭合的只剩「画面区轻微闪烁」一项，`待定因`**（人眼答"静物也闪"已排掉"帧率顿挫"并推翻"下层控件跨界"，只剩「无 VSYNC 撕裂」与「显示路径推送内容不一致」两个；判据与后续单变量实验见本节末）。本节是 §10.11 同步基线之后的新流水线结果，二者不得混写。

**改动目的**：§10.11 的同步 `recv → decode → LVGL` 会把显示侧约 17 FPS 的节奏经 TCP 反压传回 K230。新实现把接收与显示拆成两个任务：3 个 96 KiB JPEG DMA 输入槽由 free queue 管理，容量 1 的 latest queue 只保留最新待显示帧；显示任务继续使用 RGB565 双缓冲并在 `lvgl_port_lock()` 内换屏。生产者覆盖旧待显示帧时主动增加 `drop`，不让过期画面排队累积。

#### 10.12.1 C5 冷启动恢复

上板前一度只到 SDIO `Card init success`，没有 `slave chip id`，且运行态自动 reattempt 会触发 `TRANSPORT_FAILURE` 与 P4 重启环。固定 3 秒应用层延时和自动 reattempt 均已回退。整板断电至少 5 秒后重插，再用 `-NoReset` 被动观察，实测恢复：

```text
I (...) eh_init_evt: slave chip id: 0x17 (esp32c5)
I (...) eh_init_evt: esp-hosted fw versions: host=3.0.5 coprocessor=2.12.9
I (...) video: got ip:192.168.169.2 gw:192.168.169.1
I (...) video: PING SUMMARY tx=3 rx=3 loss=0% ... -> PING_OK
```

这证明**本次**故障可由主机、协处理器和 SDIO 一起冷启动清除；没有证明所有 init-event 故障都能靠断电修复。

#### 10.12.2 180 秒真机证据

| 指标 | latest-frame 实测 | 判读 |
|---|---:|---|
| K230 发送 | **10193 帧 / 91,163,085 B / 180.016 s** | `VIEW RESULT: PASS` |
| 发送端平均 | **56.62 FPS / 4.05 Mbps** | 不再被显示端约 17 FPS 反压 |
| P4 接收 | **10193 帧，`bad=0`** | 与发送端 10193/10193 闭合 |
| P4 显示 | **2981 帧，约 16.5 FPS** | 显示侧仍按自身节奏运行 |
| 主动淘汰 | **7212 帧** | latest queue 丢旧留新，属于设计行为 |
| 帧账本 | **`2981 + 7212 = 10193`** | 每个有效接收帧都有去向 |
| 解码 | **`decode_fail=0`** | 硬件 JPEG 解码长跑无失败 |

发送端汇总与接收端最终行：

```text
VIEW sent 10193 frames 91163085 bytes in 180016 ms -> 56.62 fps  4.05 Mbps
VIEW RESULT: PASS
W (...) video: link closed after 10193 RX frames (total 10193, shown 2981, drop 7212, bad 0)
I (...) video: SHOW 6.9 fps | shown 2981 drop 7212 | decode 3834 us fail 0
```

完整采集窗口内未见 LVGL lock timeout、panic、assert、brownout、`TRANSPORT_FAILURE` 或异常重启。末尾唯一一次 `link closed` 发生在 K230 正常结束 180 秒发送之后，随后 P4 继续按设计重连，不能误判为长跑中途掉线。

**结论**：接收/显示解耦和主动淘汰路径已获真机定量验证。与同步基线相比，接收端从约 17 FPS 恢复到约 57 FPS，而显示端保持约 16～17 FPS；`drop` 是“保最新”的账本，不是网络丢包。未注入发送端时间戳，故**绝对端到端延迟仍未测量**。

**人眼结果（2026-07-28）**：右侧画面持续实时变化、颜色正常（无红蓝互换）、方向正确（无镜像/旋转）均已确认。曾发现屏幕中间有固定线、运动的手跨线时出现前后帧重叠，源码配置当时为 RGB888 双局部绘制缓冲、每块整屏 1/4、90° PPA 旋转且 `avoid_tearing=false`。

**✅ 单变量实验已收口（2026-07-28 真机 + 人眼）**：只把 AXS15260 的 LVGL `buffer_px` 从整屏 1/4 扩到整屏（JPEG、网络、双缓冲、旋转、`avoid_tearing` 一律不动），ESP-IDF v5.5.4 编译并烧入 COM7，静态界面 61.5～63 FPS。整板断电 ≥5 s 重插后 `-NoReset` 确认 C5 `slave chip id: 0x17` / `192.168.169.2` / `PING_OK`，随后三轮推流（1116 + 1142 + 3425 = **5683 帧**，末轮 60 s / 57.08 FPS / 4.52 Mbps）中由人反复把手跨过原中线：**固定中线消失、跨线前后帧重叠消失**，实时/颜色/方向不回退；同轮 P4 累计 `shown=1461 / drop=4222` 与发送数逐帧闭合、`bad=0`、`decode_fail=0`、无 panic/assert/brownout/`TRANSPORT_FAILURE`/LVGL lock timeout。
⇒ **根因确认 = 局部分块刷新缝**（局部绘制缓冲 + 软件旋转 + 无防撕裂 ⇒ 撕裂被钉在分块边界）。原备选的 VSYNC / 整帧翻页实验**作废，不必做**。

**⚠️ 随后暴露的独立症状（`待定因`）：画面区轻微闪烁**，与已修掉的固定线无关，不得混写。已读源码确认的几何事实：LVGL 逻辑屏 1280×452（`init_rotation=90`、`sw_rotate=true`），canvas 在 `TOP_LEFT (640,0)`、640×480 占右半屏；**左侧下层控件确实跨进画面区** —— `s_bar_beat` 宽 **1180**（x 26..1206、y 180..194，每 **100 ms** 改值），`s_lbl_imu`/`s_lbl_sd`/`s_lbl_video` 未设宽度且文本很长（video 那行每 300 ms 重写）也会越过 x=640；canvas 最后创建 ⇒ 盖在它们之上。
**候选收敛（2026-07-28，四个 → 定因到一个）**：
- **❌ ③「上屏帧率低导致顿挫」排除** —— 用户答"动的不动的都闪"；帧率顿挫在静止画面上必然消失。
- **⬇️ ①「下层控件跨界」降级** —— 上面那条几何事实为真，但那条带是「背景 + 条 + 盖在上面的 canvas」**在缓冲里合成好后一次推送**，屏幕看不到中间态 ⇒ 推不出闪。**几何缺陷为真，却推不出这个症状**（仍值得收拾，属整洁问题）。
- **❌ ④「双缓冲翻页导致内容不一致」被 IDF 源码排除（我自己提的候选，实读推翻）** —— `components/esp_lcd/dsi/esp_lcd_panel_dpi.c:480+` 的 `dpi_panel_draw_bitmap`：只有当**传入缓冲落在面板 framebuffer 地址范围内**时才走 no-copy 分支并改写 `cur_fb_index`；我们的 LVGL 缓冲独立分配 ⇒ 走 `do_copy`，**目标恒为 `fbs[cur_fb_index]` 而该索引永不变** ⇒ **`num_fbs = 2` 的第二块 FB 从未被使用（白占约 1.7 MB），全程无翻页**。
- **✅ 只剩 ②「无 VSYNC 撕裂」，与"只有画面区闪、调试区不闪"吻合** —— 拷贝目标就是**正在被 DSI DMA 扫描输出的那块 FB** ⇒ 撕裂只出现在被拷矩形内。画面区每秒被拷约 17 次 × 640×452×3 ≈ **868 KB**；调试区只有几行小字偶尔重画 ⇒ 只有画面区看得见。
- **📖 顺带两条端口源码事实（`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`）**：`:400-408` —— `direct_mode=false && full_refresh=false` 时**一律 `LV_DISPLAY_RENDER_MODE_PARTIAL`**（**缓冲大小不决定刷新模式，flag 才决定**）；`:643-693` —— `sw_rotate` 下**每个失效区域**单独 PPA 旋转再 `draw_bitmap`。
- **⚠️ 留痕**：用户曾先报"整个屏幕都闪"、随后更正为"只有摄像头区闪"；我基于前者下过"不是撕裂"的判断，**已作废**。关键形态判据被更正 ⇒ 结论必须跟着翻。

**❌ 「只动 `avoid_tearing = true`」这条修法已被端口源码推翻（2026-07-28，别照做）**：
- `esp_lvgl_port_disp.c:643-676` —— `sw_rotate` 开启时 flush 里 `color_map` 被**换成 PPA 输出缓冲**再交给 `draw_bitmap` ⇒ 即便 `avoid_tearing` 把面板 FB 当绘制缓冲，**递进 IDF 的指针仍不是 FB**，照样 `do_copy` 进正在扫描的 FB。**只要端口做 90° 旋转，"FB 直绘 + VSYNC 翻页"这条路就堵死。**
- `:364-408` —— `avoid_tearing` 分支**只**换缓冲来源 + 建 `trans_sem`，**不设** `direct_mode`/`full_refresh`；渲染模式仍走 if/else 链 ⇒ 单开它会落到 **`RENDER_MODE_PARTIAL` 却拿面板 FB 当绘制缓冲**（局部渲染写在缓冲起始位置而非绝对坐标）= **会画花的组合**。
- 已核 sdkconfig 真值：`CONFIG_LVGL_PORT_ENABLE_PPA=y` · `LV_DEF_REFR_PERIOD=15`（≈66 Hz 上限）· `LV_USE_PERF_MONITOR=y`（屏右上角 `56 FPS / 100% CPU` 即此）· `LV_COLOR_DEPTH=16` 而面板色彩格式为 RGB888（3 B/px ⇒ 单次画面区拷贝 640×452×3 ≈ 868 KB）。

**下一步（两步走，第 2 步待拍板）**：
1. **先钉因果（约 10 min、零风险、单变量）**：把画面上屏频率临时压到极低（2 fps，或第一帧后冻结）。判据：拷贝次数/秒 ↓ ⇒ 闪烁次数同步 ↓；**完全冻结后应彻底不闪**。把「撕裂」从推理升级为真机证据。
2. **真修法（约 2 h，⛔待拍板）= 去掉端口旋转**：`sw_rotate=false` + `init_rotation=0` + `avoid_tearing=true` + `direct_mode=true` ⇒ LVGL 直接画进面板 FB、按 VSYNC 翻页，**不存在"往活动 FB 拷贝"**，撕裂构造上消失，并省掉每帧 PPA 旋转 + 868 KB 拷贝（顺带解 `100% CPU`）。代价：UI 重排成 452×1280 竖屏（与屏实际摆法一致）；640×480 视频用 PPA 缩到 452 宽，**缩放写进我们自己的 canvas 缓冲、不碰活动 FB**。
   **验收判据**：闪烁消失 + 无新增 panic/lock timeout + 改动前后 LVGL FPS 与 SHOW FPS 两个数字对账。

**⚠️ 方向预警（2026-07-28 已向用户提出，等回话）**：本线是支线、机器侧已 PASS、观感只剩"轻微"；而省赛只剩约 1 天、主线天猛星小车仍卡在救砖且从未落地。**建议只做第 1 步即停手，第 2 步列赛后待办。**

证据文件：[`firmware/esp_hosted_c5/wireless_display_latest_frame_PASS_2026-07-28.txt`](firmware/esp_hosted_c5/wireless_display_latest_frame_PASS_2026-07-28.txt)。

### 10.13 按键触发图传 + SD 录像/回放：两条真机硬事实（2026-07-29）

**① K230 用户按键 = GPIO53，下拉输入，按下为高电平 —— 真机 PASS。**
来源：立创 wiki [`lushan-pi-k230/basic/gpio-fpioa.html`](https://wiki.lckfb.com/zh-hans/lushan-pi-k230/basic/gpio-fpioa.html)（内容已改写）；本板实测抓到 **6 次按压边沿**，`baseline=0` 与文档"松开=0"一致。
顺带同页事实：板载 RGB **红=GPIO62 / 绿=GPIO20 / 蓝=GPIO63，均低电平点亮**（Lite-K230D 版按键是 GPIO64、RGB 为 65/66/71 且高电平点亮，别混用）。
⚠️ **必须去抖**：实测单次按压仅 **110~160 ms**、连按间隔 200~380 ms ⇒ 裸边沿会把一次按压算成多次切换。
⚠️ 定位方法留档：先用 `FPIOA.get_pin_func()` 导出全部 64 脚当前功能，再用 `get_pin_cfg()` 看 pad 配置。**我当时用"`oe:0` 才算输入脚"去筛，把正确答案 GPIO53 筛掉了**（它当时 `oe:1`，因为还没人把它配成输入，处于默认态）——筛选判据不能下得比事实窄。

**② ⛔ 本条标题与机理已被推翻，见 ⑧ —— 原标题"SDMMC 路径与 C5 无线不能共存"是我编的因果。**
真相：**无 C5 的固件上 SD 同样失败在同一行**（`boot_imu_sd.txt`，2026-07-27 05:36，`already initialized` 出现 0 次）⇒ "共存冲突"从来不是根因，SD 从第一次上电起就没应答过。下面两轮实验的**观测**是真的，**解释**是错的。

| 实验 | 配置 | 结果 |
|---|---|---|
| A | SD slot0 4-line、默认 20 MHz | `sdmmc_host_init: SDMMC host already initialized, skipping init flow` → card init **成功**（CID/CSD 读到）→ `sdmmc_read_sectors_dma ... 0x107(TIMEOUT)` → `failed to mount card (13)` |
| B | 同上但 `max_freq_khz=40000`（对齐 C5 时钟） | 失败**提前**到 `sdmmc_init_ocr: send_op_cond ... 0x107`，比 A 更差 |

⛔ ~~机理：P4 的 slot0/slot1 共用同一颗 SDMMC 控制器与时钟分频器；C5 的 SDIO 在 908 ms 就把它初始化在 40 MHz 并持续使用，SD 侧 host init 被跳过、拿不到自己探测需要的 400 kHz。~~ **整段作废（见 ⑧）**：`already initialized` 只是一行无害提示，不是根因 —— 2026-07-27 那份**无 C5**的日志里 SD 失败在**完全相同**的 `send_op_cond (1) returned 0x107`。我把"同时出现"当成了"因果"，并在这个自造因果上投了一整晚（22:23→23:49 的 SPI/bitbang 全线）。
⛔ ~~**副产品事实（有用）**：实验 A 的 card init 成功 ⇒ 卡座确实接在 slot0 的 CLK43/CMD44/D0-3=39,40,41,42，卡在位且能应答。~~ **本条已作废**（理由见 ⑤ 的假结论 #2：host init 被跳过，那次很可能在跟 slot1 的 C5 说话）。引脚映射后来由**上拉扫描**与**原理图逐脚核对**两条独立证据坐实，见 ⑥；但"卡在位且能应答"至今**没有**任何有效证据。

**出路 = SD 走 SPI 模式**（`sdcard_mount_spi()`，`SPI2_HOST`，零硬件改动：`SCLK=43 MOSI=44 MISO=39 CS=42`），完全避开 SDMMC 控制器。工程里 SPI 完全空闲（屏走 MIPI-DSI、触摸走 I2C）。
**当前状态：SPI 路线三发全败，已停止投入（2026-07-29 实测）**。已编译烧录（`BUILD_EXIT=0`/`FLASH_EXIT=0`），挂载结果依次为：① `ESP_ERR_INVALID_RESPONSE`；② 加内部上拉后同样 `INVALID_RESPONSE`；③ **整板断电 ≥5 s 冷启动后变成 `ESP_ERR_TIMEOUT`**。
**③ 的时间戳是最有信息量的一条**：`I (1381) 挂载 SD(SPI) ...` → `E (1402) ... ESP_ERR_TIMEOUT`，**仅 21 ms 就结束**。正常卡初始化要跑 CMD0/CMD8/ACMD41 并重试，不可能这么快 ⇒ **MISO 上根本没有任何回应**，问题不在"卡不认 SPI 模式"，而在 SPI 信号没和卡对上。
（诚实边界：③ 的日志头是 `rst:0x17 (CHIP_USB_UART_RESET)` —— 我开串口时又复位了一次，读到的是重插后**第二次**启动；但卡确实断过电，结论不受影响。）
**仍未排除的三个变量**：① 这张卡本身（部分卡在 SPI 模式下挑，换卡是 1 分钟且诊断价值最高的一测）② 板上这几个脚缺 SPI 模式需要的**外部**上拉（内部上拉已试、无效）③ SDMMC 外设是否仍占着 39–44（C5 起来时把整个 host 初始化了）。
**④ 换新卡 + 冷启动 + 原始 SPI 探针 —— A 方案判死（2026-07-29，硬件层结论）**：固件里加了 `sdcard_spi_raw_probe()`（绕过 sdspi 驱动手发 CMD0 并打原始 MISO 字节）。实测：
```
RAWPROBE dummy_rx: FF FF FF FF FF FF FF FF FF FF
RAWPROBE cmd0_rx : FF FF FF FF FF FF FF FF FF FF FF FF FF FF
RAWPROBE VERDICT: no response on MISO (all 0xFF)
```
MISO 全程被上拉在高、卡一个字节都没回；随后 sdspi 挂载照旧 21 ms `ESP_ERR_TIMEOUT`。两条独立路径（自写原始探针 + IDF sdspi 驱动）都看不到卡的任何响应。⛔ ~~而 SDMMC 模式明明能读出 CID/CSD —— 这个不对称是判据：这几个脚在 SDMMC 外设下工作正常、在 GPSPI 下完全不通 ⇒ 它们是 SDMMC 的 IOMUX 固定脚，在本板/本驱动组合上无法改路由到 SPI 外设。~~ **这半句已作废**：它的大前提（"SDMMC 模式能读出 CID/CSD"⇒"脚在 SDMMC 下工作正常"）已被 ⑤ 的假结论 #2 推翻，那次读到的 CID/CSD 可能来自 slot1 的 C5。**"不对称"从未成立，因此"IOMUX 无法路由到 GPSPI"这个说法没有证据支撑，别再引用。**（诚实边界：我的原始探针有个已知缺陷——`spi_device` 会自动拉低 CS，所以上电后"CS 拉高时给 ≥74 clock"这一步没做到；但 IDF 的 sdspi 驱动是正确做了这步的，它同样失败，故结论不依赖我这个探针的完备性。）
⇒ **A 方案（P4 侧存卡）判死，转 B。** `P4_ENABLE_SDCARD` 已回置 0，`sdcard_mount_spi()` 与探针代码保留作留档，别再重试这条路。

**⑤ 换卡后继续深挖，结论收敛到物理层（2026-07-29，用户明确"不用 B"后继续攻 A）**：

| 测量 | 结果 |
|---|---|
| **外部上拉扫描**（逐脚拉低后放开、看是否被快速拉回高；只扫 IO39~54） | **`39 40 41 42 43 44` 恰好 6 个有外部上拉**（另有 53），与原理图「SD_D0/D1/D2/D3/CMD/CLK 各 51 kΩ 上拉」完全吻合 ⇒ **卡座确实在 39–44，"引脚可能不对"的假设被客观证伪** |
| **纯 GPIO bitbang + SD 规范时序**（CS 高 + 80 clk → CS 低 → CMD0 + CRC7 `0x95`），30 轮 / 60 s，期间人为插拔 | **全部 `FF FF ...`，总线从未被任何器件驱动** |

**⚠️ 两个我自己造的假结论，已修正、留档防重犯**：
1. 第一版 bitbang 探针放在 `spi_bus_initialize()` **之后** ⇒ 43/44/39 已被 GPSPI 接管，`gpio_set_level` 根本没在动引脚，"bitbang 也全 FF"是**探针自己失效**。移到 SPI 初始化之前才有效（结论未变，但当时那一轮不算证据）。
2. 早先 SDMMC 那次「card init 成功、读出 CID/CSD」**不能作为"卡是活的"的证据** —— `sdmmc_host_init` 被跳过（host 已由 C5 初始化），那次很可能是在跟 **slot1 上的 C5** 说话，这也解释了为何随后 `read_sectors` 超时。**引用它推理过的地方全部作废。**

**⇒ 当时的结论**：引脚对、上拉在、时序合规、不涉任何外设，而总线从未被驱动 ⇒ 倾向物理解释（卡未接触 / 卡未上电）。用户已确认：① 插的确实是 P4 那块板 ② 是开发板自带卡槽、卡能插紧 ③ 没有读卡器，无法在电脑上验卡。另用 **K230 的已知 good 启动卡**换上重试，仍 15 轮全 `FF`。
**⚠️ 但这个结论至今缺一个决定性判据，不可当已定论 —— 见 ⑥。**
**代码现状**：`P4_SD_PROBE_LOOP_SEC=60`（诊断模式，开机 60 s 只探测不挂载、不跑图传），定论后须置 0 恢复正常固件。

**⇒ 结论与出路**：**P4 侧存卡不再作为主路**。推荐改走 **K230 侧存卡**——K230 的 SD 卡本来就在正常工作（它从这张卡启动），MicroPython 直接 `open()` 写文件；P4 只做实时显示 + 触摸 UI（这部分已 PASS）；回放走同一条已验证 TCP 链路（P4 请求 → K230 按帧发回，暂停/拖动 = 告诉 K230 从第几帧开始发）。代价仅是录像文件在 K230 卡里。

**⑥ 原理图逐脚核对完成 —— 图纸确认了引脚，但也暴露出我漏做的两项基础测量（2026-07-29，PDF 级 / 非真机）**

此前 ①~⑤ 全部结论都建立在**软件侧探测**上，我从未真正读过原理图（只做过 PDF 文本提取，并当场以"文字邻接不可信"为由放弃）。本轮把 [`资料/ESP32P4-DevBoard_Schematic_2026-07-14.pdf`](资料/ESP32P4-DevBoard_Schematic_2026-07-14.pdf) 的相关两页**渲染成图看完**（page 0 主页、page 9 = `MK-MicroSD(P4)` 子图，卡座型号 `TF-115-BCP9`）。

图上确认（page 0，ESP32P4 N32R32 符号顶部一排引脚，网络标签按 x 与引脚名一一对齐）：

| P4 引脚 | 网络 | 卡座 pin | SPI 模式角色 | 与代码是否一致 |
|---|---|---|---|---|
| IO44 | `SD_CMD` | 3 `CMD` | MOSI | ✅ |
| IO43 | `SD_CLK` | 5 `CLK` | SCLK | ✅ |
| IO42 | `SD_D3` | 2 `CD/DATA3` | CS | ✅ |
| IO41 | `SD_D2` | 1 `DATA2` | — | ✅ |
| IO40 | `SD_D1` | 8 `DAT1` | — | ✅ |
| IO39 | `SD_D0` | 7 `DAT0` | MISO | ✅ |

同时图上得到的其它硬事实：

- **`VDD`(pin4) 直连 `+3.3V`，没有负载开关 / LDO / 使能脚**，只挂 `C52 10uF` + `100nF` 去耦 ⇒ **"板子没给卡供电"这一支排除**（除非虚焊）。此前我说"VDD 有电"的唯一依据是"上拉在"，属于推断，现已由图纸独立确认。
- **`CD`(pin9) 画着 No-Connect ⇒ 本板没有机械卡检测信号**，软件层面无从得知卡是否插入。
- 6 个上拉 `R12/R20/R21/R22/R23/R24` 全 **51 kΩ 上 `+3.3V`**，与 ⑤ 的上拉扫描"恰好 6 个"吻合。
- 信号路径上**无串阻、无 0Ω、无电平转换器、无 DNP**；`IO45/46/47` 标 NC，`IO39~44` 无 NC。
- 子图端口名 `SD1_D1` 映射到顶层 `SD_D1`（层次图两侧命名不一致，是合法连接，不是断线）。
- ⑤ 里上拉扫描多扫出来的那个 **53 = `C5_BOOT`**（ESP32-C5 的 BOOT 脚），与 SD 无关。

**🔴 卡内上拉方向此前记反了（已核实纠正）**：SD 卡在 `CD/DAT3` 上带的是**卡内约 50 kΩ 上拉**（上电即使能，`ACMD42` 可断开），**不是下拉**。来源：[EE.SE microSD 上下拉](https://electronics.stackexchange.com/a/39578)、[Datakey CD/DAT3 卡检测](https://datakey.com/support/topics/how-to-detect-when-a-dfx-ruggedrive-memory-token-sd-card-is-inserted)、[trezor ACMD42](https://github.com/trezor/trezor-core/issues/287)（均已改写摘录）。**后果**：本板 `SD_D3` 已有 51 kΩ 外部上拉到 3.3 V，插卡后卡内 50 kΩ 上拉并到**同一条轨** ⇒ **靠读电平高低完全区分不出插卡与否**，"电平法卡检测"在本板是死路，别再尝试。

**🔴 两个从未验证的前提（①~⑤ 的全部 `FF` 都压在它们上面）**

| # | 未验证前提 | 为什么致命 | 该怎么测（都不依赖 SD 协议） |
|---|---|---|---|
| A | **我真的在驱动那 6 个引脚** | 三条路径全 `FF` 的共同解释可以是"CLK 从未翻转 / CS 从未拉低"。⑤ 已经栽过一次同类跟头（探针放在 `spi_bus_initialize()` 之后，`gpio_set_level` 根本没动引脚） | **输出自回读**：逐脚配推挽输出，拉低读回应为 0、拉高读回应为 1。任一脚拉低后读回仍是 1 ⇒ 该脚未被我驱动（被外设占用 / 驱动失效），此前所有结论对该脚作废 |
| B | **卡与卡座之间有电气接触** | `TF-115-BCP9` 是推推式；"插紧"可能只是插到摩擦位而非锁定位。这与"卡座虚焊"是两个完全不同的根因（一个是操作问题，一个要返修），但现有证据无法区分 | **RC 上升时间比值法**（见下） |

**RC 比值法（为什么它是干净判据）**：把脚拉低放电，再切成输入且关掉内部上下拉，用 `esp_cpu_get_cycle_count()` 数到读出高电平为止，得上升时间 `t ≈ R_eff × C_line`。

- `IO42`(D3)：不插卡 `R=51k`；插卡后与卡内 50 kΩ 上拉**并联** ⇒ `R≈25.3k`
- `IO41`(D2)：插卡前后都是 `R=51k`（卡内只有 D3 有那个上拉）
- 取比值 **`t(IO42) / t(IO41)`**：插卡前应 ≈ **1.0**，插卡后应 ≈ **0.5**
- 妙处在于**线上寄生电容与卡的输入电容在比值里被约掉**，不必知道 C 是多少，也不受绝对时间刻度影响

⇒ **判据**：插卡后比值仍 ≈1.0 ⇒ 卡与卡座无电气接触（卡座坏 / 卡没到位）；掉到 ≈0.5 ⇒ 接触良好，`FF` 的根因在别处。

**⇒ ⑥ 当时的结论状态**：`P4 侧 SD 卡座硬件不通` = 高度怀疑但 `待验证`，缺 A/B 两项引脚级测量。**A/B 已于同日跑完，结果见 ⑦。**

读图工具（临时，落 `.tmp_pdf/esp32p4/`，未入库）：`sch_scan.py`（按关键字定位页）· `sch_grep.py`（页内 grep + 同行邻居，追网络名）· `sch_area.py`（按矩形 dump 文本、`--vert` 支持竖排引脚标签）· `sch_dump.py`（带坐标文本 + 高 DPI 裁剪渲染 PNG）。⬜ 若后续还要读别的原理图（天猛星载板也有），值得合并成一个脚本挪进 `tools/`。

**⑦ 引脚级诊断真机结果 —— 结论收口：P4 侧 microSD 不可用属硬件层（2026-07-29，真机）**

固件：`P4_ENABLE_SDCARD=1` + `P4_SD_PROBE_LOOP_SEC=30` 的诊断版（跑完已复位回 `0/0` 并烧回正常固件，实测 LVGL 稳定 **62.5 FPS**、无诊断日志）。原始采集：`.tmp_pdf/esp32p4/padtest_card_in.txt`、`ident_card_in.txt`、`restore_normal.txt`（临时目录，未入库）。卡 = 用户原本那张开发板自带卡槽用的卡，**全程在位**。

| 测量 | 真机结果 | 判定 |
|---|---|---|
| **前提 A · 输出自回读** | `PADDRIVE D0/39=01 D1/40=01 D2/41=01 D3/42=01 CLK/43=01 CMD/44=01` | ✅ **6/6 全部跟随（拉低读回 0、拉高读回 1）⇒ 我确实在驱动这 6 个脚**。前提 A 成立，①~⑤ 的 `FF` 证据不是"探针没动引脚"造成的 |
| **前提 B · RC 上升时间比值** | 6 脚上升时间 105~115 cyc，而轮询开销本身 108~114 cyc @360 MHz | ⚠️ **INCONCLUSIVE —— 这是我方法的缺陷，不是硬件结论**。`gpio_config()` 做输入/输出切换的耗时远大于 RC 时间，线在第一次采样前就到高了。要救得改用单条寄存器写（`GPIO_ENABLE1_W1TC`）替代 `gpio_config()`，或直接上示波器。**没有硬拗这组数** |
| **⭐ SDMMC 身份识别 @400 kHz / 1-line** | `host_init -> ESP_OK` · `init_slot(0) -> ESP_OK` · `sdmmc_init_ocr: send_op_cond (1) returned 0x107` · `card_init FAILED: ESP_ERR_TIMEOUT` | 🔴 **规范识别速率、最少变量、IDF 官方驱动 ⇒ slot0 的 CMD 线上没有任何 SD 卡应答** |

**🔴 顺带解掉了 ② / ④ 那个"SDMMC 明明能通"的印象（第 3 个同类自伤）**：回查原始日志 `.tmp_pdf/esp32p4/sdtest_boot2.txt`，**里面根本没有任何 CID/CSD 输出** —— "读到 CID/CSD"是我自己加的修饰。日志真正支撑的只是"没在 card init 阶段报错、走到 `read_sectors` 才失败"。而本轮同一个 SDMMC 驱动在**更保守**的配置（400 kHz / 1-line）下明确失败于 `send_op_cond` ⇒ 第一轮之所以没报错，极可能是 `sdmmc_card_init` 流程里的 **CMD5（SDIO 探测）命中了 slot1 的 ESP32-C5** 并走了 SDIO 分支。**⇒ 作废 ④ 的决定是对的，理由到此补齐。**

**⇒ ⛔ 本节结论已于 2026-07-30 被真机推翻，见 ⑨。** 下面这段保留作错误留档 —— ~~P4 侧 microSD 不可用，根因在硬件层（卡座触点 / 焊接 / 走线），软件治不了~~。当时的四条"独立路径"确实都失败了，但它们**共享同一个我从未检查的前提**：SDMMC IO 域根本没上电。原文：

1. IDF `sdspi` 驱动 —— 21 ms 即 `ESP_ERR_TIMEOUT`
2. GPSPI 原始探针 —— MISO 全 `FF`
3. 纯 GPIO bitbang（按 SD 规范 CS 高 + 80 clk → CS 低 → CMD0/CRC7 `0x95`）—— 15 轮全 `FF`
4. **IDF SDMMC @400 kHz 1-line —— `send_op_cond` 超时**

且四条都建立在已被独立确认的基础上：引脚映射（原理图逐脚 + 上拉扫描）· 引脚可驱动（`PADDRIVE` 6/6）· `VDD` 直连 `+3.3V` 无使能开关（原理图）· 6×51 kΩ 上拉在位（扫描）。另换用 **K230 的已知 good 启动卡**同样全 `FF`。

**⬜ 唯一保留的不确定**：手上没有读卡器，无法在电脑上直接验卡，故不能 100% 排除"两张卡同时坏"（概率极低——K230 那张能正常引导 K230）。若日后拿到读卡器，验一次即可闭合。

**⑧ SD 卡这条线的完整历程与失误复盘（2026-07-30，用户要求整理）**

全部时间戳取自 `.tmp_pdf/esp32p4/` 的采集文件（未入库，可能被清），内容为原始串口日志。

| 时刻 | 动作 | 日志关键行 | 我当时的判断 |
|---|---|---|---|
| **07-27 05:36** | SDMMC 4-line 首次尝试（**固件里还没有 C5 无线**，`already initialized` 出现 **0 次**） | `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | 归因为"卡没插好/换卡试试"，**没有深挖，也没留档** |
| 07-29 22:11 | 合入 C5 后重试 SDMMC 20 MHz | `SDMMC host already initialized, skipping init flow` → `read_sectors_dma 0x107` → `mount (13)` | 🔴 **看到 `already initialized` 就编出"C5 抢了 host 和时钟分频器"** |
| 07-29 22:17 | 把 SD 时钟对齐成 40 MHz | `already initialized` + `send_op_cond 0x107` | 认为"失败提前 ⇒ 更坐实共存冲突" |
| 07-29 22:23~22:31 | 转 SPI 模式（3 轮：裸跑 / 加内部上拉 / 冷启动） | `INVALID_RESPONSE` ×2 → `ESP_ERR_TIMEOUT`（**仅 21 ms**） | 21 ms 太快 ⇒ MISO 无回应（**这个判断是对的**） |
| 07-29 22:46 | 换卡 + 自写 GPSPI 原始探针 | `RAWPROBE VERDICT: no response on MISO (all 0xFF)` | 判 A 方案死、转 B |
| 07-29 22:51~23:01 | 纯 GPIO bitbang（按 SD 规范时序） | 全 `FF` | 🔴 第一版探针放在 `spi_bus_initialize()` **之后**，引脚已被 GPSPI 接管 ⇒ **探针自己没动引脚**，那轮不算证据 |
| 07-29 23:05 | 外部上拉扫描 | `PULLUPSCAN pins_with_ext_pullup: 39 40 41 42 43 44 53` | ✅ 引脚定位客观成立（6 个 + 53） |
| 07-29 23:49 | 换 K230 已知 good 启动卡，15 轮 | 全 `FF` | 判"卡座物理不通"，写进 `.h` 注释与 README |
| **07-30 01:0x** | **用户质问"你确定你查看了原理图吗"** → 渲染 PDF 读图 | 图上确认 IO44/43/42/41/40/39 = CMD/CLK/D3/D2/D1/D0；`VDD` 直连 `+3.3V` 无使能开关；`CD` 标 NC | ✅ 引脚与供电第二条独立证据 |
| 07-30 01:21 | `PADDRIVE` 输出自回读 | 6 脚全 `01` | ✅ 排除"我根本没在驱动引脚" |
| 07-30 01:2x | `PADRC` 上升时间比值 | 105~115 cyc vs 轮询开销 108~114 cyc | ⚠️ 判据分辨率不足，**INCONCLUSIVE，未硬拗** |
| 07-30 01:3x | `IDENT` SDMMC @400 kHz 1-line | `send_op_cond (1) returned 0x107` | 🔴 与 **07-27 那次一模一样** |
| 07-30 01:4x | **回查 07-27 日志** | 无 C5、`already initialized`=0、同一行失败 | ⛔ **"共存冲突"整套解释崩塌** |

**⇒ 修正后的结论（比原结论更硬）**：从 07-27 第一次上电到 07-30，**跨越有/无 C5 两种固件、SDMMC/GPSPI/bitbang 三种总线、两张不同的卡，SD 从来没有应答过任何一条命令**。⇒ P4 侧 microSD 不可用，根因在硬件层。而**"SDMMC 与 C5 共存"这个议题根本不存在** —— 无 C5 时 SD 也不工作，别再去"验共存"。

**🔴 失误清单（按代价排序，写在这里防重犯）**

1. **手上有反证却不查，凭"同时出现"编因果**（最贵，约 1.5 小时白走）。`already initialized` 与失败同时出现，我就当它是原因；而 07-27 的反证日志**就在同一个目录里**，查一眼 1 分钟。⇒ **规矩：新故障先 `Get-ChildItem` 翻同一部件的历史采集，比写任何新代码都便宜。**
2. **把推断写成实测，4 次**：① "CID/CSD 读到"（日志里没有）② "卡在位且能应答"（由①推出，双重虚构）③ "IOMUX 无法路由到 GPSPI"（大前提是虚构的"不对称"）④ "原理图里看不到 IOxx 编号"（图上写得很清楚）。⇒ **规矩：写"实测"二字前，把那行日志贴出来。贴不出来就写"推断"。**
3. **跳过最便宜的一手证据，先跑最贵的实验**。读原理图零成本 5 分钟，我跳过并给自己找了理由（"文字邻接不可信"）；**是用户质问之后才去读的**。⇒ **规矩：碰硬件问题，原理图和历史日志排在改代码之前。**
4. **探针自身未先验证**。bitbang 第一版放在 `spi_bus_initialize()` 之后，`gpio_set_level` 打在已被外设接管的引脚上。⇒ **规矩：自造测量工具必须先有一个"工具活着"的判据**（这次的 `PADDRIVE` 就是补上的那个）。
5. **判据方向记反 + 分辨率不足**。卡内 `CD/DAT3` 是上拉（不是下拉，幸而搜证纠正）；`PADRC` 的 `gpio_config()` 切换耗时 >> RC 时间。⇒ **规矩：判据要自带"够不够分辨"的自检**（这次打了 `poll_overhead` 才没把噪声当信号）。
6. **结论措辞过硬**。"实测：本板 microSD 卡座物理不通"曾直接写进 `sdcard.h` 注释——那是下个对话会当事实读的地方。⇒ **规矩：证据还压在未验证前提上时，写"高度怀疑 + 待验证 + 缺哪一项"。**

**✅ 也要记住哪些是真有价值的**（免得复盘变成全盘否定）：引脚映射被两条独立证据坐实 · `PADDRIVE` 排除了整类自伤 · `IDENT @400 kHz/1-line` 是最干净的单点判据 · 最终结论是对的。**错的不是每一步的测量，是选择走哪一步的依据。**

### 10.14 ✅ SD 卡真相：P4 的 SDMMC IO 域必须显式上电（2026-07-30 真机 PASS）

**根因**：ESP32-P4 的 SDMMC IO 供电域是**外部供电**（`components/soc/esp32p4/include/soc/soc_caps.h`：
`#define SOC_SDMMC_IO_POWER_EXTERNAL 1  ///< SDMMC IO power controlled by external power supply`），
必须显式用**片上 LDO_VO4** 给它上电。我此前从未做过这一步，所以 pad 推不出有效电平。

```c
#if SOC_SDMMC_IO_POWER_EXTERNAL
    const sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
    sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_ctrl);
    host.pwr_ctrl_handle = s_pwr_ctrl;
#endif
```

通道 4 的依据：IDF 头文件 `sd_pwr_ctrl_by_on_chip_ldo.h` 原话是把它设成 `4` 对应 `LDO_VO4` 给
SDMMC IO 供电；本工程 `sdkconfig` 里 `VO1=Flash(3300mV)`、`VO2=PSRAM(1800mV)`、屏的 MIPI DSI PHY
占 `ch3(2500mV)`，**通道 4 空闲**。

**发现途径（值得记住）**：用户贴出上游仓库截图并问"你真的有试过这个吗" —— 上游
`gitee.com/Ergou-/esp32-p4-c5-aibox` 分支 `xiaozhi-p4c5`、`main/boards/sila-p4c5/sd_scanner.c`
里就有这段。**本工程本身就是从这个仓库 clone 的**（`origin` 指向它，分支 `mipi_lcd`），
而我整晚没去看过一眼同仓库其它分支怎么把 SD 跑起来的。上游 `config.h` 的引脚与本文件逐个一致
（`CLK43 CMD44 D0-3=39,40,41,42`），本可当第一手交叉验证。

**真机结果（`.tmp_pdf/esp32p4/ldo_fix.txt`）**

| 项 | 值 |
|---|---|
| 挂载 | ✅ `挂载成功: SD 59638 MB 20000 kHz 4-line` |
| 卡 | `Name: SD` · `Type: SDHC` · `CSD ver=2, sector_size=512, capacity=122138624` |
| 顺序写 | **499 KB/s** |
| 顺序读 | **1435 KB/s** |
| 与 C5 无线共存 | ✅ 同一次启动里 `eh_sdio: Card init success, TRANSPORT_RX_ACTIVE` + `WLAN over SDIO` 正常 |

**⛔ 由此一次性作废的三个结论**（都曾被我写进代码注释或文档，是下个对话的地雷）：

1. ~~"本板 microSD 硬件层不可用"~~ —— 卡一直是好的，是我没给 IO 域上电。
2. ~~"SDMMC 与 C5 无线不能共存，必须走 SPI"~~ —— **同一次启动里两者都正常**。`sdmmc_host_init:
   SDMMC host already initialized, skipping init flow` **照样出现**，SD 却挂载成功 ⇒ 那行是无害提示，
   我却把它当成了根因并据此改了整条技术路线。
3. ~~"这几个脚是 SDMMC IOMUX 固定脚，无法路由到 GPSPI"~~ —— 前提本就不成立。

**🔑 第二个坑（紧接着的）：59638 MB 的卡只挂出 9 MB —— 是分区，不是卡满**

LDO 修好后挂载成功，但 `esp_vfs_fat_info` 报 **总 9 MB / 可用 2 MB**，把测速量提到 8 MB 就
`E sdcard: 写第 78 块失败`（78 × 32 KB ≈ 2.4 MB，与"剩 2 MB"精确吻合）。**先前 2 MB 的测速能过，
纯粹是刚好卡在临界点上，把问题蒙过去了。** 只读 dump MBR（`sdcard_dump_mbr()`）看清了真相：

| 分区 | 类型 | LBA | 扇区数 | 容量 | 内容 |
|---|---|---|---|---|---|
| p1 | FAT32 (0x0C) | 102400 | 20480 | **10 MB** | `CONFIG.TXT` + 9 个 `GC2093*.XML/.JSON`（摄像头 ISP 标定）+ `System Volume Information`，已用 7 MB |
| p2 | FAT32 (0x0C) | 122880 | 1048515 | 511 MB | 未查 |
| **p3** | FAT32 (0x0C) | 2097152 | 120041471 | **58613 MB** | **空** |
| p4 | — | — | — | — | 空 |

FatFs 在 `VolToPart[].pt == 0`（自动）时只挑**第一个** FAT 分区 ⇒ 一直挂在那个 10 MB 上。
**修法：IDF 本来就开了 `FF_MULTI_PARTITION=1`，`VolToPart` 是非 static 全局（`ff.h` 有 extern），
挂载前写一句 `VolToPart[0].pt = 3` 即可** —— 见 `sdcard.h` 的 `P4_SD_PARTITION`（默认 3）。
**不需要格式化，不动卡上任何现有数据**（GC2093 那些配置文件仍在 p1 原封不动）。
⚠️ 换卡后必须重新 `sdcard_dump_mbr()` 再定这个值，别照抄 3。

**✅ 最终真机结果（`.tmp_pdf/esp32p4/ldo_p3.txt`）**

| 项 | 值 |
|---|---|
| 挂载 | `SD 59638 MB 40000 kHz 4-line`，`文件系统: 总 58606 MB / 可用 58606 MB` |
| 顺序写（8 MB 稳态） | **415 KB/s** |
| 顺序读 | **1551 KB/s** |
| 与 C5 无线共存 | ✅ 正常 |

写速与总线时钟基本无关（20 MHz→499、40 MHz→517，都是 2 MB 小样本；8 MB 稳态 **415**）⇒
**瓶颈在卡和 FAT，不在时钟**。

**⚠️ 由此产生的真实约束**：稳态写 **415 KB/s < 图传码率约 460 KB/s**（§10.12）⇒ **全帧率录像会跟不上**，
而且录像时还要同时收网络、解码、上屏，实际更紧。⬜ 对策（未实施）：**抽帧录**（如每 2~4 帧存 1 帧），
带宽降到 100~230 KB/s；原速回放靠文件里的时间戳，抽帧不影响"原速"，且用户已明确"画质/内容大致即可"。

**⇒ 方案回滚**：录像放回 **P4 侧**，不必走 K230 侧存卡。已写的 `k230_recplay.py` 不作废 ——
它的录像文件格式（`u32 时间戳 | u32 长度 | JPEG`）与原速回放逻辑可直接搬到 P4 侧复用。

### 10.15 P4 侧录像模块（`recorder.c/.h`）—— 写盘链路真机 PASS（2026-07-30）

**设计要点（为什么这么做，不是随手写）**

- **旁路 + 独立任务**：`recorder_feed()` 由 RX 任务调用，内部只做"抽帧判断 → 拷一份到 PSRAM →
  非阻塞入队"，真正的 `fwrite` 在优先级 3 的 `rec_wr` 任务里。理由：稳态写 424 KB/s 下一帧
  约 8 KB 要 20 ms，塞进 RX 循环会直接反压网络接收。**队列满就丢帧并计数，绝不阻塞 RX。**
- **抽帧 `REC_EVERY_N=4`**：图传约 55 fps，全存需要约 460 KB/s > 写速 424 KB/s ⇒ 必然堆积。
  抽到约 14 fps（约 92 KB/s）留 4.6 倍余量。**原速回放靠文件里的时间戳，抽帧不影响原速。**
- **不借用 `rx_slot` 的所有权**：调用方马上要把 slot 还回去给下一帧，所以必须拷贝。
- **序列号取"现有最大号 +1"**（不是"文件数 +1"）：删掉中间文件也不会撞名覆盖已有录像。
- **卡满即停**：每 60 帧查一次 `esp_vfs_fat_info`，低于 `REC_MIN_FREE_MB=64` 就自己停并打日志。

**真机结果（`.tmp_pdf/esp32p4/rec_emb.txt`）—— 用内嵌帧灌录 20 s**

| 项 | 值 | 校验 |
|---|---|---|
| 写入帧数 | **278** | — |
| **丢帧** | **0** | ✅ 写盘完全跟得上 |
| 抽帧跳过 | 834 | 278/(278+834) = **25%**，与 1/4 精确一致 |
| 时长 | 19943 ms | ✅ 与设定 20 s 一致 |
| 文件 | `/sdcard/REC00001.MJP` **1843974 B** | ✅ **= 278 × (6625 + 8)**，精确吻合"8 字节帧头 + JPEG"格式 |
| 实效帧率 / 码率 | 13.9 fps / 约 92 KB/s | 相对 424 KB/s 写速余量 4.6× |
| 剩余空间 | 58605 MB | 卡满逻辑就绪未触发 |

⚠️ **这一轮用的是内嵌的 `k230_frame1.jpg`（6625 B），不是实拍画面** —— K230 当时不在线
（`GetPortNames()` 里只有 COM7）。所以本节只证明**录像模块与写盘链路正确**，
**端到端"实拍→录像"仍 `待验证`**：需要 K230 起 AP + 跑图传，`rec_autotest_task` 会自动
优先用实时帧（判据是 `video_stream` 的 `frames > 10`）。

⬜ **遗留**
- 回放（读文件、按时间戳原速送解码）与触摸 UI 小按钮**都还没做**。`k230_recplay.py` 里的
  回放/暂停/拖动逻辑可直接搬（文件格式两边一致）。
- `P4_REC_AUTOTEST_SEC=20` 是临时触发器，UI 做好后应置 0。
- 小瑕疵：`_BENCH.BIN 0 B` 偶尔残留在卡上（测速的 `remove` 偶发没删掉），不影响功能。

### 10.16 ⚠️ 端到端录像被 C5 挡住：C5 持续不发 esp-hosted init event（2026-07-30，未解决）

**症状**：P4 启动后 `E video: esp_wifi_init: ESP_FAIL (C5 co-processor not answering over SDIO?)`
→ `no link -- video not started` ⇒ 无线图传起不来 ⇒ 录像只能落到内嵌帧兜底，
**端到端"实拍→录像"至今 `未验证`**。

**已经排除的（都是真机单变量）**

| 假设 | 判据 | 结论 |
|---|---|---|
| SDIO 硬件层坏了 | 日志有 `eh_host_port_sdio: Function 0/1 Blocksize: 512` + `eh_sdio: Card init success, TRANSPORT_RX_ACTIVE` + `SDIO Host operating in STREAMING MODE` | ❌ 排除，**硬件层通的** |
| SD 卡挂载抢了 SDMMC 控制器 | 把 `P4_ENABLE_SDCARD` 改回 0 重烧，**3/3 仍失败** | ❌ 排除，与 SD 无关 |
| 间歇性、多复位几次就好 | 连续 **6 次** RTS 复位，`init_evt=False` 6/6 | ❌ 排除，是**持续性**故障（我一度判成"间歇"，已作废） |
| 是我这轮 SD/录像改动引入的 | 成功那轮(`rec_e2e2.txt`)与失败轮(`rec_e2e4.txt`)**是同一个固件**（`build Jul 30 2026 16:50:17`），且日志到 `2615ms Starting SDIO process rx task` **逐行连时间戳都一致** | ❌ 排除 |

**故障点定位**：正常应在 `2615ms` 之后紧接着出现 ——

```
I (2646) eh_init_evt: slave chip id: 0x17 (esp32c5)
I (2646) eh_init_evt: capabilities: 0x0d
I (2648) eh_init_evt: SDIO mode: slave=streaming host=streaming
I (2648) eh_auto_init: auto_init: initialising 'wifi' (priority 150)
```

失败时 **2615ms 之后一片空白**，直到 3505ms 的 LVGL 心跳。⇒ **SDIO 传输层能读到 C5 的 CIS，
但 C5 的应用固件没有发出 init event** ⇒ 判断 C5 的 CPU 卡住了，而 `Reset co-processor using
GPIO[36]` + P4 的 RTS 复位都救不回来（那两者都做了，日志里有）。

**⬜ 下一步（待做）**
1. **整板断电重插**（不是 RTS 复位）—— 针对"C5 固件卡死需冷启动"这个唯一未排除的假设。
2. 若冷启动仍不行：C5 侧 `esp-hosted` 版本本就不匹配（日志 `E eh_init_evt: major version
   mismatch — OTA coprocessor from host`，`host=3.0.5 / coprocessor=2.12.9`），
   之前是 degraded 状态在跑，需要给 C5 刷匹配版本的 slave 固件。

**⚠️ 顺带修掉的一个自伤（值得记）**：这一轮排查里我用 `p4_boot_read.ps1` **默认带 RTS 复位**去
观察一个正在传输的连接，把已经建立好的 TCP 掐断了 —— README 与交接里早写过"观察运行中系统
必加 `-NoReset`"，我自己违反了。判据：K230 侧明明打出 `STREAM accepted from ('192.168.169.2', 64939)`
+ `camera+encoder running`，而 P4 侧同时报 wifi init 失败，两边矛盾就是复位掐断的指纹。

**⚠️ 另一个坑（入库脱敏的代价）**：`k230_ap_up.py` 里 `KEY = "<K230_AP_PSK>"` 是**脱敏占位符**，
14 字符刚好满足 WPA2 ≥8 所以 `ap.config()` 不报错，但与 P4 的 `CONFIG_P4V_WIFI_PASSWORD` 不匹配
⇒ P4 侧表现为 `StaDisconnected reason=14 (MIC_FAILURE) rssi=-18`（信号很强却连不上）。
**复演时必须先把占位符换成真实 PSK**（本地做法：从 `p4_lcd/sdkconfig` 读出来生成
`.tmp_pdf/` 下的临时副本，密码不入库）。

**⑥ 续查（2026-07-30 深夜）：换了四种测量，结论收敛到"C5 只在整板上电时工作"**

⚠️ **先作废我自己的两个中间结论**：① "C5 固件卡死" —— 不准确，见下 ② "把 `RX_STAGING_SLOTS`
降到 1 修好了 dma_alloc" —— **假的**，Kconfig 写着 `range 2 8`，1 被钳回 2；那轮 `dma_fail=False`
只是因为 **C5 那次干脆没发数据、没触发分配**，不是我修好了（又一个假成功判据）。

| 测量 | 做法 | 结果 |
|---|---|---|
| **DMA 内存水位探针** | `main.c` 加 `dma_watermark()`，打 `MALLOC_CAP_DMA` 的 free + **largest** | 🔑 `app_main` 开头 largest 就只有 **253952 B (248 KB)**，而 C5 要 **278528 B (272 KB)** ⇒ **起点就不够**。**SD 只吃 2 KB**（322843→320787）⇒ 之前怀疑"SD 挤爆内存"是错的 |
| `RX_STAGING_SLOTS` 2→1 | 改 sdkconfig | ❌ Kconfig `range 2 8` 钳回 2。help 明写内存 = `slots × max-aggregate` |
| `CP_BRINGUP_ON_TIMEOUT` NONE→**REATTEMPT** | 让 esp_hosted 超时后自动重试复位 C5 | ❌ **更糟**：第二次 `Reset co-processor using GPIO[36]` → `failed to read registers` → P4 自己重启 → **重启循环**（日志 11 KB→32 KB，多轮 `transport_init`，全部 `init_evt=False`）⇒ **已回退，别开这个** |
| `SDIO_RX_OPT` STREAMING→**NONE** | 不做 RX 聚合，就不需要那对 136 KB staging buffer | ✅ 配置生效（`SDIO Host operating in **PACKET MODE**`，编译产物 `sdkconfig.h` 里 `CONFIG_ESP_HOSTED_HOST_SDIO_RX_NONE 1` 已确认）、**`dma_alloc` 失败消除**；但 `init_evt` 仍 False ⇒ **证明 C5 不发数据与 DMA 内存无关** |

**⇒ 收敛结论**：**C5 只在整板断电上电后的第一次启动里正常**（`eh_init_evt: slave chip id: 0x17`
→ `initialising 'wifi'` → `joining K230_AP`，实测一次成功）；**之后每次 RTS 复位 P4 都失败**，
7+ 次一致。`eh_sdio` 的传输层每次都正常（`Card init success` / `PACKET MODE` / `Open data path`），
**只有 C5 的应用层 init event 不来**。GPIO36 软复位（`CP_RESET_STRATEGY_ALWAYS` +
`SETTLE_MS=1500`）不足以复现上电时序。⇒ **判为 C5 的上电/复位时序问题，P4 侧软件治不了。**

**🔧 保留的配置改动（⚠️ `sdkconfig` 在 `workbench/esp32p4/` 下、被 gitignore，换机重建必须手动补）**
- `CONFIG_ESP_HOSTED_HOST_SDIO_RX_NONE=y`（原 `..._RX_STREAMING_MODE=y`）—— 消除
  `eh_sdio: dma_alloc(278528) failed; dropping read`。代价是不做 RX 聚合，而图传只需约
  460 KB/s、SDIO 40 MHz 有约 20 MB/s 余量，**够用但吞吐上限未实测**。
- `CONFIG_ESP_HOSTED_HOST_CP_BRINGUP_ON_TIMEOUT_NONE=y` **保持不变**（别改 REATTEMPT）。

**⬜ 端到端录像的可行流程（未实测，下次照此做）**：不能复位 P4，所以利用"断电重插时 P4 自己启动"这一次机会 ——
1. 整板断电重插（P4 启动后录像自测有 **100 s** 等待窗口）
2. 立刻起 K230：`k230_ap_up_real.py`（约 20 s）→ 图传脚本（约 18 s 到 listening），合计约 38 s < 100 s
3. **全程不要复位 P4**；要看日志只能 `p4_boot_read.ps1 -NoReset`
4. P4 收到 >10 帧即自动录 20 s 实拍，判据 = 新 `REC*.MJP` 的大小 **≠ 1843974 B**（那是内嵌帧的指纹）

### 10.17 端到端图传+录像：卡点定位到「C5 要 272 KB 连续 DMA，板上只有 248 KB」（2026-07-30 深夜真机）

**⚠️ 板上固件状态不确定**：最后一条"改 sdkconfig→编译→烧录"被中断。产物 `p4_mipi_lcd.bin`
（22:19:57）里已是 `CONFIG_ESP_HOSTED_HOST_SDIO_RX_MAX_SIZE 1`，但**没拿到 `FLASH_EXIT`**
⇒ 下次接手**先重烧一次确定基线**，别在不确定的固件上判读。

**✅ 这轮确定下来的（冷启动后一次跑通到 TCP）**

P4 侧链路全通：
```
I (2665) eh_init_evt: SDIO mode: slave=streaming host=streaming
I (2886) video: wifi sta started, joining SSID:K230_AP
I (3953) video: got ip:192.168.169.2 gw:192.168.169.1
I (6957) video: connected -- RX producer running
```
K230 侧完全正常（`.tmp_pdf/esp32p4/k230_short.txt`）：
```
STREAM accepted from ('192.168.169.2', 60273)
STREAM camera+encoder running, streaming 45000 ms
STREAM sent 6 frames 163906 bytes in 1855 ms -> 3.23 fps  0.71 Mbps
STREAM send error: OSError(104,)      # ECONNRESET，是 P4 断的
```
⇒ **相机/编码/发送都对，约 27 KB/帧**（比 §10.12 记的 8 KB/帧大得多，画面内容不同）。

**🔴 卡点**：P4 报 `link closed after 0 RX frames (total 0, shown 0, drop 0, **bad 0**)`。
`bad 0` 排除了 magic 错与长度非法 ⇒ 是 `recv` 本身拿不到数据，与
`W eh_sdio: dma_alloc(278528) failed; dropping read` 对应：**TCP 握手包小所以连接建得起来，
但 27 KB 的图像帧需要大 staging buffer，分配失败就被整包丢弃。**

**关键量化（`dma_watermark()` 实测）**

| 阶段 | DMA free | DMA **largest** |
|---|---|---|
| app_main 开头 | 322843 | **253952 (248 KB)** |
| after_sd | 320787 | 253952（**SD 只吃 2 KB**） |
| after_lvgl | 268919 | 229376 |
| after_video | 244711 | 204800 |

C5 的 STREAMING 模式要 **278528 B (272 KB)**，而**开机第一行 largest 就只有 248 KB**。
开 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` 后这个数**一个字节都没变** ⇒ **248 KB 是内部
SRAM 最大连续段的上限，不是"被谁占了"，省不出来**（`MALLOC_CAP_DMA` 与 `MALLOC_CAP_INTERNAL`
的 largest 恒等 ⇒ DMA 内存就是内部 RAM，PSRAM 不计入）。

**⛔ 我在这轮制造的一次失败（不是硬件问题，别误记）**：为省内存把 host 改成
`CONFIG_ESP_HOSTED_HOST_SDIO_RX_NONE=y`（packet 模式），而 C5 slave 侧仍是 streaming ⇒
`SDIO mode: slave=streaming host=packet` 不匹配 ⇒ **P4 重启 13 次**（`transport_init` 13 次 /
`init_evt` 12 次）。已回退成 `RX_STREAMING_MODE`。**结论：host 与 slave 的 RX 模式必须一致。**

**⛔ 同时再确认一遍（本轮又复现 2 次）**：**每次烧录 / RTS 复位都会毁掉 C5**（`init_evt` 不来），
**只有整板断电能恢复**。所以任何需要图传的验证，都必须"先摆好 K230 → 断电重插 → 全程不复位 P4"。

**⬜ 下一步（按性价比，都不用猜）**
1. 重烧确定基线 → 断电一次 → 试 `CONFIG_ESP_HOSTED_HOST_SDIO_RX_MAX_SIZE=y`（choice 里唯一
   没试过的；它是"固定最大包"而非聚合，**可能不需要 272 KB**）。判据先看 `SDIO mode:` 那行两边
   是否一致，再看有无 `dma_alloc ... failed`。
2. 若仍不行，验这个怀疑：**§10.12 那次 180 s 图传 PASS 时，固件里没有 FATFS/SDMMC**。
   把 `P4_ENABLE_SDCARD=0` **且**从 `main/CMakeLists.txt` 摘掉 `fatfs`/`sdmmc`/`esp_driver_sdmmc`
   依赖，看 `largest` 是否回到 >272 KB。若是 ⇒ 根因是"链接进 SD 组件改变了内存布局"，
   那就得在 SD 与图传之间取舍，或让 SD 改走 SPI（不拉 FATFS 的 DMA 池）。
3. 不受影响、已经能用的：SD（58606 MB / 稳态写 424 KB/s）· 录像模块（278 帧丢 0）·
   回放模块 `player.c/h`（编译过，**真机未验**）。
