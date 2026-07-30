// -*- coding: utf-8 -*-
// microSD（SDMMC slot0, 4-line）挂载 + 顺序读写测速。
//
// 引脚：ESP32-P4 的 SDMMC slot0 走 IOMUX 固定脚 —— CLK=43 CMD=44 D0=39 D1=40 D2=41 D3=42
// （见 IDF soc/sdmmc_pins.h，与官方 sd_card 例程 P4 默认值一致）。
// ✅ 2026-07-29 已用原理图逐脚核对确认（`资料/ESP32P4-DevBoard_Schematic_2026-07-14.pdf`
//    page 0 芯片符号顶排：IO44=SD_CMD IO43=SD_CLK IO42=SD_D3 IO41=SD_D2 IO40=SD_D1
//    IO39=SD_D0；page 9 = MK-MicroSD(P4) 子图，卡座 TF-115-BCP9）。
// ⛔ 原注释说"原理图里看不到 IOxx 编号"是错的 —— 那是我当时没读图编的解释，图上写得很清楚。
// 顺带：GPIO47 = slot0 的 D6（仅 8-line 用），出厂固件驱动它当背光是无效操作。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// ⚠️ 合入无线图传（方案 D）后 microSD 默认关闭，改 1 可开回来。
//
// 原因：卡走 SDMMC **slot0**，而板载 ESP32-C5 协处理器走同一个 SDMMC 控制器的
// **slot1**（CLK18/CMD19/D0-D3=14..17）。「两个 slot 能不能同时用」在本板上
// **未验证** —— 之前 sdkconfig 里那句"把 C5 放 slot1 就能和 SD 卡共存"是我自己
// 写的推断，不是实测结论。无线图传是主线，不能让一个未验证的共存假设当它的
// 前置变量；等图传稳定了再单独验共存（那是一次干净的单变量实验）。
// ⛔⛔ 2026-07-30：**"本板 microSD 硬件层不可用"这个结论是错的，已作废。**
//     真正的根因是软件：**ESP32-P4 的 SDMMC IO 供电域必须显式用片上 LDO_VO4 上电**
//     （`soc_caps.h: SOC_SDMMC_IO_POWER_EXTERNAL=1`），我此前从未做过这一步。
//     ⇒ 修法见 sdcard.c 里 sdcard_mount() 的 sd_pwr_ctrl_new_on_chip_ldo(ldo_chan_id=4)。
//     发现途径：用户指出上游工程有 SD 实现 —— gitee.com/Ergou-/esp32-p4-c5-aibox
//     分支 xiaozhi-p4c5、main/boards/sila-p4c5/sd_scanner.c（其引脚定义与本文件逐个一致）。
//
// 为什么之前四条路径全失败却指向了错误结论：**没上电的 pad 推不出有效电平**，而我用来
// "排除硬件"的两个判据恰好都对这件事免疫 ——
//   · PADDRIVE 自回读 6/6 通过：只证明 P4 内部逻辑自洽，**不测 pad 的绝对电压**
//   · 外部上拉扫描 6 个脚全被拉高：那是板载 3.3V 经 51kΩ 干的，与 P4 的 IO 域是两条电源
//   · 卡座 VDD 直连 3.3V（原理图）⇒ **卡一直是活的**，只是收不到命令，于是 MISO 被上拉
//     钉在高 = 全 0xFF / send_op_cond 超时。换卡、有无 C5 自然都一样。
// ⛔ 同时作废："SDMMC 与 C5 无线不能共存"（07-27 无 C5 的日志里失败在同一行）、
//    "SDMMC 模式能读出 CID/CSD"（原始日志里没有 CID/CSD）。完整复盘见 README §10.13 ⑧。
//
// ⬜ **当前状态 `待验证`**：LDO 供电已加入并编译通过，但**真机尚未跑通挂载**。
//    判据 = 启动日志出现 `SDMMC IO 供电已就绪 (on-chip LDO_VO4)` 且随后 `挂载成功: <卡名>
//    <容量>MB`；在看到这两行之前，不要把"SD 可用"当事实。
// 🔧 sdcard_pad_drive_selftest()/sdcard_pad_rc_report()/sdcard_sdmmc_identify() 保留备查，
//    但要记住它们的排除力边界（见上）。pad_rc_report 另有分辨率不足问题
//    （gpio_config 切换耗时 >> RC 时间），要用得先改成单条寄存器写或上示波器。
// ⚠️ 连带作废：「SDMMC 与 C5 无线共存」这件事**从未被真正测过** —— 之前那两轮 SDMMC
//    失败很可能只是卡座不通，不是共存冲突。别再引用那两轮当"共存不成立"的证据。
// 🔧 诊断开关用法：置 1 且 P4_SD_PROBE_LOOP_SEC>0 = 只跑引脚级诊断（不挂载不抢引脚，
//    图传照常）。2026-07-30 置 1 是为了验证 LDO_VO4 供电修复，PROBE_LOOP 保持 0 走正常挂载。
#ifndef P4_ENABLE_SDCARD
#define P4_ENABLE_SDCARD 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     mounted;
    char     name[16];      // 卡名(CID)
    uint64_t size_mb;       // 卡容量 MB（≠ 可用空间，别混用）
    uint64_t total_mb;      // 文件系统总容量 MB
    uint64_t free_mb;       // 文件系统可用 MB —— 录像"卡满即停"看这个
    int      speed_khz;     // 实际时钟
    int      bus_width;
    bool     is_sdio;
    bool     is_mmc;
} sdcard_info_t;

typedef struct {
    bool  done;
    float write_kbps;
    float read_kbps;
    uint32_t bytes;
} sdcard_bench_t;

// 最近一次测速结果（供 UI 轮询显示；未跑过时 done=false）
const sdcard_bench_t *sdcard_bench_result(void);

// 挂载到 /sdcard。**绝不自动格式化**（format_if_mount_failed=false）——
// 用户卡里可能有数据，挂不上就报错，不能拿别人的卡冒险。
esp_err_t sdcard_mount(void);

// SPI 模式挂载（与 C5 无线共存的唯一可行路径，见 sdcard.c 里的实测留档）。
// SDMMC 路径在图传固件里必然失败，图传工程一律调这个。
#define SDCARD_SPI_HOST SPI2_HOST
esp_err_t sdcard_mount_spi(void);

// >0 = 诊断模式：每 2 s 循环跑一次 bitbang 探针，共这么多秒，期间不挂载。
// 用途：让人边插拔卡边看日志，不必和串口采集时间对齐。诊断完置 0。
// 挂载哪个 MBR 分区（1..4，0=让 FatFs 自动挑第一个 FAT 分区）。
// 本卡实测三个 FAT32 分区：p1=10MB(GC2093 配置,仅剩2MB) / p2=511MB / p3=58613MB。
// 录像要大空间 ⇒ 默认 3。换卡后**必须重新看 MBR**（sdcard_dump_mbr()）再定这个值。
#ifndef P4_SD_PARTITION
#define P4_SD_PARTITION 3
#endif

#ifndef P4_SD_PROBE_LOOP_SEC
#define P4_SD_PROBE_LOOP_SEC 0
#endif

const sdcard_info_t *sdcard_info(void);

// 只读 dump MBR 分区表：卡容量 59638MB 但只挂出 9MB，用它看清 59GB 在哪个分区。
void sdcard_dump_mbr(void);

// 列出卡根目录内容（只读，不改动任何文件）。用来判断卡上现有数据能不能动。
void sdcard_list_root(void);

// 顺序写再读回 total_kb 千字节，算吞吐。会在 /sdcard 下创建并删除 _bench.bin
// 实际测量量会按可用空间自动收缩（见实现里的注释）。
esp_err_t sdcard_benchmark(uint32_t total_kb, sdcard_bench_t *out);

#ifdef __cplusplus
}
#endif
