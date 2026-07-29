# ESP-Hosted（P4 主机 + 板载 C5 协处理器）三工程的重建差异

> 工程本体在 `workbench/esp32p4/`（`c5_cp` / `p4_scan_host` / `p4_sta_host`），按 `.gitignore` **不入库**（第三方例程 + managed_components 体积大）。
> 所以这里只存**重建所必需的差异**：三个 `sdkconfig.defaults`、被改过的 `main.c` 与 `main/CMakeLists.txt`、钉死版本的 `idf_component.yml`，外加两份**真机 PASS 的启动日志**当证据。
> 背景、引脚论证、判据、踩坑全在 [`../../README.md` §九](../../README.md)，本文只讲"怎么把工程重建出来"。

## 重建（换机 / 误删后）

```powershell
# 1. 建工程（例程名来自组件仓 API；'slave' 在 esp_hosted 3.x 已不存在）
powershell -File ..\..\tools\create_hosted_projects.ps1 -Feature "wifi/scan"
powershell -File ..\..\tools\create_hosted_projects.ps1 -Feature "wifi/sta"
# 得到 workbench\esp32p4\{c5_scan_cp, p4_scan_host, c5_sta_cp, p4_sta_host}
# 两个 cp 逐文件只差一行注释 -> 只留一个，改名 c5_cp，另一个删掉
```

```powershell
# 2. 放回本目录的差异（覆盖同名文件）
copy c5_cp.sdkconfig.defaults          ..\..\..\esp32p4\c5_cp\sdkconfig.defaults
copy p4_scan_host.sdkconfig.defaults   ..\..\..\esp32p4\p4_scan_host\sdkconfig.defaults
copy p4_sta_host.sdkconfig.defaults    ..\..\..\esp32p4\p4_sta_host\sdkconfig.defaults
copy p4_sta_host.main.c                ..\..\..\esp32p4\p4_sta_host\main\main.c
copy p4_sta_host.main.CMakeLists.txt   ..\..\..\esp32p4\p4_sta_host\main\CMakeLists.txt
copy c5_cp.idf_component.yml           ..\..\..\esp32p4\c5_cp\main\idf_component.yml
copy p4_scan_host.idf_component.yml    ..\..\..\esp32p4\p4_scan_host\main\idf_component.yml
copy p4_sta_host.idf_component.yml     ..\..\..\esp32p4\p4_sta_host\main\idf_component.yml
# p4_sta_host.sdkconfig.defaults 里的 SSID/PSK 是占位符 <SSID>/<PSK>，
# 真值在 workbench/天猛星主板平台/无线遥测_ESP01S链路.md（本仓库不重复登记密码）
```

```powershell
# 3. 编译（必要时先按 ../../README.md §9.3 给 IDF 打 SDIO 补丁——只有编 c5_cp 才需要）
powershell -File ..\..\tools\build_hosted.ps1 -Project c5_cp
powershell -File ..\..\tools\build_hosted.ps1 -Project p4_scan_host
powershell -File ..\..\tools\build_hosted.ps1 -Project p4_sta_host
# 4. 上板 + 判读
powershell -File ..\..\tools\hosted_bringup.ps1 -Project p4_scan_host
powershell -File ..\..\tools\hosted_bringup.ps1 -Project p4_sta_host
```

## 文件清单

| 文件 | 是什么 | 相对上游例程的改动 |
|---|---|---|
| `c5_cp.sdkconfig.defaults` | C5 协处理器固件配置 | **未改**（原样，仅供对账） |
| `p4_scan_host.sdkconfig.defaults` | P4 扫 AP 主机配置 | **+本板适配块**：CP target=C5 / SDIO slot1 + 六个引脚 / reset=36 / console=USB-JTAG / **`SELECTS_REV_LESS_V3` + `REV_MIN_100`（本板是 v1.3 早期片，不加就烧不进）** |
| `p4_sta_host.sdkconfig.defaults` | P4 连 AP 主机配置 | 同上 + SSID/PSK（此处**已抹成占位符**） |
| `p4_sta_host.main.c` | 主机 App | 上游例程 + 本地增改，全部带 `LOCAL` 注释：STA_CONNECTED 事件（把"没连上"与"连上但没 IP"分开）· 有界等待替代 `portMAX_DELAY`（原版拿不到 IP 就永久静默挂着）· 打印 `esp_wifi_get_mac`/`esp_netif_get_mac` · DHCP 超时退静态 IP · **ICMP ping 网关 + 一行 `PING SUMMARY ... PING_OK/PING_FAIL` 判据** · 删掉上游把 PSK 打进日志那两行 |
| `p4_sta_host.main.CMakeLists.txt` | 组件依赖 | `PRIV_REQUIRES` 加 `lwip`（`ping/ping_sock.h`） |
| `*.idf_component.yml` | 依赖清单 | `espressif/esp_hosted` 版本从 `'*'` **钉死 `3.0.5`** |
| `boot_log_scan_PASS_2026-07-27.txt` | 真机证据 | 扫到 51 个 AP、`slave chip id: 0x17 (esp32c5)` |
| `boot_log_sta_ping_PASS_2026-07-27.txt` | 真机证据 | `PING SUMMARY tx=5 rx=5 loss=0% -> PING_OK` |

> 上游例程为 Apache-2.0（Espressif），`main.c` 保留原始版权头。

---

## 附：`p4_lcd` 合并固件（收帧 + 硬件解码 + 上屏）的重建差异

`p4_lcd` 是屏工程，本体也在 `workbench/esp32p4/`（不入库）。方案 D 最后一段把无线收帧合进了它，
所以这里多存一组 `p4_lcd.*` 前缀的文件。**基线**：屏工程原始的 6 个原创源码在 `../`
（`ai_panel.{c,h}` / `imu_qmi8658.{c,h}` / `sdcard.{c,h}`），上游差异在 `../upstream_local.patch`。

> **`../upstream_local.patch` 已于 2026-07-28 重新生成**（原版停在 flash16MB/IMU/SD/video 之前、早已落后于工程）。
> 现版覆盖 10 个上游文件、含**完整 `sdkconfig`**（这是刻意的：本工程**绝不能跑 `idf.py set-target`**，
> 否则按 defaults 重生成 sdkconfig 会静默丢掉 P4 rev-min、PSRAM HEX 200MHz+XIP、TASK_WDT off、LVGL PPA 旋转这些关键项）。
> **已验**：在 `git worktree add --detach <tmp> HEAD` 出来的干净树上 `git apply --check` 退出码 **0**，且主工作树全程未被触碰。
> ⚠️ **patch 里的 `CONFIG_P4V_WIFI_PASSWORD` 是 `<PLACEHOLDER>`**（真值只在 `workbench/天猛星主板平台/无线遥测_ESP01S链路.md` 一处登记）——
> apply 之后必须手填自己的 PSK 才能连上 K230 的 AP，否则表现为「关联不上/一直重连」。抹除做过正反双向自检 + 全仓 188 个已跟踪文件反扫，0 命中。

```powershell
# 放回顺序：先按 ../../README.md §六 把屏工程重建出来，再覆盖下面这些
copy p4_lcd.sdkconfig.defaults       ..\..\..\esp32p4\p4_lcd\sdkconfig.defaults
copy p4_lcd.main.c                   ..\..\..\esp32p4\p4_lcd\main\main.c
copy p4_lcd.main.CMakeLists.txt      ..\..\..\esp32p4\p4_lcd\main\CMakeLists.txt
copy p4_lcd.main.idf_component.yml   ..\..\..\esp32p4\p4_lcd\main\idf_component.yml
copy p4_lcd.main.Kconfig.projbuild   ..\..\..\esp32p4\p4_lcd\main\Kconfig.projbuild
copy p4_lcd.jpeg_view.c              ..\..\..\esp32p4\p4_lcd\main\jpeg_view.c
copy p4_lcd.jpeg_view.h              ..\..\..\esp32p4\p4_lcd\main\jpeg_view.h
copy p4_lcd.video_stream.c           ..\..\..\esp32p4\p4_lcd\main\video_stream.c
copy p4_lcd.video_stream.h           ..\..\..\esp32p4\p4_lcd\main\video_stream.h
copy ..\ai_panel.c ..\ai_panel.h ..\sdcard.c ..\sdcard.h ..\imu_qmi8658.c ..\imu_qmi8658.h ^
     ..\..\..\esp32p4\p4_lcd\main\
# 还要把 K230 编码器产出的那张真帧放回去（EMBED_FILES 要它，缺了编不过）：
#   ..\..\..\esp32p4\p4_lcd\main\k230_frame1.jpg  <- k230_jpeg_frame1_PASS_*.txt 里的 base64 解出来
powershell -File ..\..\tools\build_p4.ps1        # ⛔ 永远不要对 p4_lcd 跑 set-target
```

| 文件 | 是什么 |
|---|---|
| `p4_lcd.video_stream.{c,h}` | **本役新增**：WiFi STA(经 esp_hosted 到 C5) → TCP 拉 `'JF'` 分帧 → 硬件 JPEG 解码 → 双缓冲挂到 `lv_canvas`。解码引擎建一次复用；永不放弃重连 |
| `p4_lcd.main.Kconfig.projbuild` | **本役新增**：`P4V_WIFI_SSID` / `P4V_WIFI_PASSWORD` / `P4V_PING_ON_BOOT` |
| `p4_lcd.sdkconfig.defaults` | 屏配置 + **本役新增的 esp_hosted 块**（与 `p4_sta_host.sdkconfig.defaults` 同源，一字未改）。**PSK 已抹成 `<K230_AP_PSK>`** |
| `p4_lcd.main.c` | 第 7 步起 `video_stream_start(ai_panel_video_canvas())`；microSD 按 `P4_ENABLE_SDCARD` 编译期关闭 |
| `p4_lcd.main.CMakeLists.txt` | `+video_stream.c`、`REQUIRES esp_hosted`、`PRIV_REQUIRES +esp_wifi nvs_flash esp_event esp_netif lwip` |
| `p4_lcd.main.idf_component.yml` | 屏依赖 + `esp_hosted 3.0.5` / `esp_wifi_remote`（与 `p4_sta_host` 保持逐字一致） |
| `p4_lcd.jpeg_view.{c,h}` | 嵌入帧的一次性解码（现在的角色：开机自检 + 网络没通时的占位画面） |
| `k230_ap_stream_view.py` | K230 侧长跑版推流（180s + 每 2s 进度），给人眼看屏用；`k230_ap_stream.py` 是 20s 的验收版 |

> ⚠️ **合并固件端到端未验证**：编译烧录都过了，但首次上板约 4 分钟后 P4 的 USB 口整个掉了（疑供电）。
> 判据与后续动手步骤见 [`../../README.md` §10.11](../../README.md)。
