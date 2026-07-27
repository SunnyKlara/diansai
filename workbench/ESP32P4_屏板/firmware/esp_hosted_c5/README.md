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
