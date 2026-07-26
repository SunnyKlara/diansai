// -*- coding: utf-8 -*-
// microSD（SDMMC slot0, 4-line）挂载 + 顺序读写测速。
#include "sdcard.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>          // fsync()

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
// 用宏而非变量：下面要和字面量做字符串拼接（MOUNT_POINT "/_bench.bin"）
#define MOUNT_POINT "/sdcard"

// P4 SDMMC slot0 IOMUX 固定脚（见 .h 注释）
#define PIN_CLK 43
#define PIN_CMD 44
#define PIN_D0  39
#define PIN_D1  40
#define PIN_D2  41
#define PIN_D3  42

static sdmmc_card_t *s_card;
static sdcard_info_t s_info;
static sdcard_bench_t s_bench;

const sdcard_bench_t *sdcard_bench_result(void) { return &s_bench; }

esp_err_t sdcard_mount(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = PIN_CLK;
    slot.cmd = PIN_CMD;
    slot.d0  = PIN_D0;
    slot.d1  = PIN_D1;
    slot.d2  = PIN_D2;
    slot.d3  = PIN_D3;
    // 板上未接 CD/WP 信号；内部上拉当兜底(正式设计应在板上加 10k 外部上拉)
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,   // 绝不动用户卡里的数据
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "挂载 SDMMC slot0 4-line (CLK=%d CMD=%d D0..D3=%d,%d,%d,%d)...",
             PIN_CLK, PIN_CMD, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载失败：卡里没有可识别的 FAT 文件系统"
                          "（本驱动刻意不自动格式化，请在电脑上格成 FAT32 再试）");
        } else {
            ESP_LOGE(TAG, "初始化卡失败: %s —— 最常见就是**没插卡**；"
                          "其次是卡座接触/卡不支持 4-line", esp_err_to_name(err));
        }
        s_info.mounted = false;
        return err;
    }

    s_info.mounted = true;
    snprintf(s_info.name, sizeof(s_info.name), "%s", s_card->cid.name);
    s_info.size_mb = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    s_info.speed_khz = s_card->max_freq_khz;
    s_info.bus_width = slot.width;
    s_info.is_sdio = s_card->is_sdio;
    s_info.is_mmc = s_card->is_mmc;

    ESP_LOGI(TAG, "挂载成功: %s  %llu MB  %d kHz  %d-line",
             s_info.name, s_info.size_mb, s_info.speed_khz, s_info.bus_width);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

const sdcard_info_t *sdcard_info(void) { return &s_info; }

esp_err_t sdcard_benchmark(uint32_t total_kb, sdcard_bench_t *out)
{
    if (!s_info.mounted || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));

    const size_t chunk = 32 * 1024;
    uint8_t *buf = malloc(chunk);
    if (buf == NULL) {
        ESP_LOGE(TAG, "测速缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < chunk; i++) {
        buf[i] = (uint8_t)i;
    }

    const char *path = MOUNT_POINT "/_bench.bin";
    const uint32_t rounds = (total_kb * 1024) / chunk;
    esp_err_t ret = ESP_OK;

    // ---- 写 ----
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "创建 %s 失败", path);
        free(buf);
        return ESP_FAIL;
    }
    int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < rounds; i++) {
        if (fwrite(buf, 1, chunk, f) != chunk) {
            ESP_LOGE(TAG, "写第 %lu 块失败", (unsigned long)i);
            ret = ESP_FAIL;
            break;
        }
    }
    fflush(f);
    fsync(fileno(f));
    int64_t t1 = esp_timer_get_time();
    fclose(f);

    if (ret == ESP_OK) {
        out->bytes = rounds * chunk;
        out->write_kbps = (float)out->bytes / 1024.0f / ((float)(t1 - t0) / 1e6f);

        // ---- 读 ----
        f = fopen(path, "rb");
        if (f == NULL) {
            ESP_LOGE(TAG, "回读打开失败");
            ret = ESP_FAIL;
        } else {
            t0 = esp_timer_get_time();
            for (uint32_t i = 0; i < rounds; i++) {
                if (fread(buf, 1, chunk, f) != chunk) {
                    ESP_LOGE(TAG, "读第 %lu 块失败", (unsigned long)i);
                    ret = ESP_FAIL;
                    break;
                }
            }
            t1 = esp_timer_get_time();
            fclose(f);
            out->read_kbps = (float)out->bytes / 1024.0f / ((float)(t1 - t0) / 1e6f);
        }
    }

    remove(path);   // 别在用户卡上留垃圾
    free(buf);

    if (ret == ESP_OK) {
        out->done = true;
        ESP_LOGI(TAG, "测速 %lu KB: 写 %.0f KB/s, 读 %.0f KB/s",
                 (unsigned long)(out->bytes / 1024), out->write_kbps, out->read_kbps);
    }
    s_bench = *out;   // 供 UI 轮询
    return ret;
}
