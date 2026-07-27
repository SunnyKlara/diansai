// -*- coding: utf-8 -*-
// microSD（SDMMC slot0, 4-line）挂载 + 顺序读写测速。
//
// 引脚：ESP32-P4 的 SDMMC slot0 走 IOMUX 固定脚 —— CLK=43 CMD=44 D0=39 D1=40 D2=41 D3=42
// （见 IDF soc/sdmmc_pins.h，与官方 sd_card 例程 P4 默认值一致）。板上模组即以
// SD_CLK/SD_CMD/SD_D0..D3 这些名字引出这一组，所以原理图里看不到 IOxx 编号。
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
#ifndef P4_ENABLE_SDCARD
#define P4_ENABLE_SDCARD 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     mounted;
    char     name[16];      // 卡名(CID)
    uint64_t size_mb;       // 容量 MB
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

const sdcard_info_t *sdcard_info(void);

// 顺序写再读回 total_kb 千字节，算吞吐。会在 /sdcard 下创建并删除 _bench.bin
esp_err_t sdcard_benchmark(uint32_t total_kb, sdcard_bench_t *out);

#ifdef __cplusplus
}
#endif
