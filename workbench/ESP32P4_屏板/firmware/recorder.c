// -*- coding: utf-8 -*-
#include "recorder.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sdcard.h"

static const char *TAG = "recorder";

#define REC_DIR      "/sdcard"
#define REC_QDEPTH   6            // 6 × 约 8KB ≈ 48KB（走 PSRAM），够吸收写盘抖动
#define FREE_CHECK_N 60           // 每 60 帧查一次剩余空间（esp_vfs_fat_info 不便宜）

typedef struct {
    uint8_t *buf;                 // PSRAM 里的拷贝，由写盘任务负责 free
    uint32_t len;
    uint32_t ts_ms;               // 相对本次录像第一帧
} rec_item_t;

static QueueHandle_t   s_q;
static volatile bool   s_recording;
static FILE           *s_fp;
static int64_t         s_t0_us;
static uint32_t        s_feed_seq;          // 抽帧计数（feed 侧）
static recorder_stats_t s_st;

bool recorder_is_recording(void) { return s_recording; }

void recorder_get_stats(recorder_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_st;
    out->recording = s_recording;
    if (s_recording) {
        out->ms = (uint32_t)((esp_timer_get_time() - s_t0_us) / 1000);
    }
}

// 序列号命名：扫目录取现有最大号 +1。这样删掉中间文件也不会撞名，
// 而"文件数 +1"那种做法删过文件就会覆盖已有录像。
static uint32_t next_index(void)
{
    uint32_t mx = 0;
    DIR *d = opendir(REC_DIR);
    if (d == NULL) {
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        // 只认 REC#####.MJP；VFS 给的是 8.3 短名，长度固定好比对
        if (strncasecmp(e->d_name, "REC", 3) != 0) {
            continue;
        }
        uint32_t v = 0;
        int i = 3;
        while (e->d_name[i] >= '0' && e->d_name[i] <= '9') {
            v = v * 10 + (uint32_t)(e->d_name[i] - '0');
            ++i;
        }
        if (i > 3 && v > mx) {
            mx = v;
        }
    }
    closedir(d);
    return mx + 1;
}

static void flush_queue(void)
{
    rec_item_t it;
    while (xQueueReceive(s_q, &it, 0) == pdTRUE) {
        heap_caps_free(it.buf);
    }
}

esp_err_t recorder_start(void)
{
    if (s_q == NULL) {
        ESP_LOGE(TAG, "未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recording) {
        ESP_LOGW(TAG, "已经在录了");
        return ESP_OK;
    }
    if (!sdcard_info()->mounted) {
        ESP_LOGE(TAG, "SD 未挂载，不能录");
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t tot = 0, fre = 0;
    if (esp_vfs_fat_info(REC_DIR, &tot, &fre) == ESP_OK &&
        (fre / (1024 * 1024)) < REC_MIN_FREE_MB) {
        ESP_LOGE(TAG, "剩余空间只有 %llu MB(<%d)，拒绝开录",
                 fre / (1024 * 1024), REC_MIN_FREE_MB);
        return ESP_ERR_NO_MEM;
    }

    char path[40];
    snprintf(path, sizeof(path), REC_DIR "/REC%05lu.MJP", (unsigned long)next_index());
    s_fp = fopen(path, "wb");
    if (s_fp == NULL) {
        ESP_LOGE(TAG, "创建 %s 失败", path);
        return ESP_FAIL;
    }
    flush_queue();                 // 上一次的残帧不要混进新文件
    memset(&s_st, 0, sizeof(s_st));
    snprintf(s_st.path, sizeof(s_st.path), "%s", path);
    s_t0_us = esp_timer_get_time();
    s_feed_seq = 0;
    s_recording = true;
    ESP_LOGW(TAG, "开始录像 %s（每 %d 帧存 1 帧）", path, REC_EVERY_N);
    return ESP_OK;
}

void recorder_stop(void)
{
    if (!s_recording) {
        return;
    }
    s_recording = false;           // 先关闸，写盘任务看到就不再写
    vTaskDelay(pdMS_TO_TICKS(80)); // 让它把手上那帧写完
    flush_queue();
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    uint64_t tot = 0, fre = 0;
    esp_vfs_fat_info(REC_DIR, &tot, &fre);
    ESP_LOGW(TAG, "停止录像 %s：%lu 帧 / %lu KB / %lu ms，跳过 %lu，丢 %lu，剩余 %llu MB",
             s_st.path, (unsigned long)s_st.frames, (unsigned long)s_st.kbytes,
             (unsigned long)s_st.ms, (unsigned long)s_st.skipped,
             (unsigned long)s_st.dropped, fre / (1024 * 1024));
}

void recorder_feed(const uint8_t *jpg, uint32_t len)
{
    if (!s_recording || s_q == NULL || jpg == NULL || len == 0) {
        return;
    }
    // 抽帧：实测写速 415 KB/s < 图传 460 KB/s，全存必然堆积
    if ((s_feed_seq++ % REC_EVERY_N) != 0) {
        ++s_st.skipped;
        return;
    }
    // 拷一份走：调用方的 slot 马上要还回去给下一帧用，不能借出所有权
    uint8_t *cp = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (cp == NULL) {
        ++s_st.dropped;
        return;
    }
    memcpy(cp, jpg, len);
    const rec_item_t it = {
        .buf = cp,
        .len = len,
        .ts_ms = (uint32_t)((esp_timer_get_time() - s_t0_us) / 1000),
    };
    // 非阻塞入队：宁可丢这一帧，也不能让 SD 写盘反压住网络接收
    if (xQueueSend(s_q, &it, 0) != pdTRUE) {
        heap_caps_free(cp);
        ++s_st.dropped;
    }
}

static void writer_task(void *arg)
{
    (void)arg;
    rec_item_t it;
    for (;;) {
        if (xQueueReceive(s_q, &it, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // 出队后才发现已经停录（或文件已关）：丢掉，别写进已关闭的句柄
        if (!s_recording || s_fp == NULL) {
            heap_caps_free(it.buf);
            continue;
        }
        uint8_t hdr[8];
        hdr[0] = (uint8_t)(it.ts_ms);
        hdr[1] = (uint8_t)(it.ts_ms >> 8);
        hdr[2] = (uint8_t)(it.ts_ms >> 16);
        hdr[3] = (uint8_t)(it.ts_ms >> 24);
        hdr[4] = (uint8_t)(it.len);
        hdr[5] = (uint8_t)(it.len >> 8);
        hdr[6] = (uint8_t)(it.len >> 16);
        hdr[7] = (uint8_t)(it.len >> 24);
        bool ok = (fwrite(hdr, 1, sizeof(hdr), s_fp) == sizeof(hdr));
        if (ok) {
            ok = (fwrite(it.buf, 1, it.len, s_fp) == it.len);
        }
        heap_caps_free(it.buf);
        if (!ok) {
            ESP_LOGE(TAG, "写盘失败，停止录像（多半是卡满或卡掉了）");
            recorder_stop();
            continue;
        }
        ++s_st.frames;
        s_st.kbytes += (it.len + 8 + 512) / 1024;
        s_st.ms = it.ts_ms;

        // 卡满即停：用户明确要求"满了就停"。每 60 帧查一次，别每帧查。
        if ((s_st.frames % FREE_CHECK_N) == 0) {
            uint64_t tot = 0, fre = 0;
            if (esp_vfs_fat_info(REC_DIR, &tot, &fre) == ESP_OK) {
                const uint64_t free_mb = fre / (1024 * 1024);
                if (free_mb < REC_MIN_FREE_MB) {
                    ESP_LOGW(TAG, "卡将满（剩 %llu MB < %d），按要求停止录像",
                             free_mb, REC_MIN_FREE_MB);
                    recorder_stop();
                }
            }
        }
    }
}

esp_err_t recorder_init(void)
{
    if (s_q != NULL) {
        return ESP_OK;
    }
    s_q = xQueueCreate(REC_QDEPTH, sizeof(rec_item_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "队列创建失败");
        return ESP_ERR_NO_MEM;
    }
    // 优先级压在 RX/显示任务之下：录像是旁路，绝不该抢显示的 CPU
    if (xTaskCreate(writer_task, "rec_wr", 4096, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
        ESP_LOGE(TAG, "写盘任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "录像模块就绪（队列 %d，抽帧 1/%d，停录阈值 %d MB）",
             REC_QDEPTH, REC_EVERY_N, REC_MIN_FREE_MB);
    return ESP_OK;
}
