// -*- coding: utf-8 -*-
#include "player.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdcard.h"
#include "video_stream.h"

static const char *TAG = "player";

#define REC_DIR    "/sdcard"
#define HDR_BYTES  8              // u32 ts_ms + u32 len

static FILE          *s_fp;
static SemaphoreHandle_t s_lock;  // 保护 s_fp 与下面这组状态
static player_state_t s_state;
static uint32_t       s_clip_index;
static uint32_t       s_frame;
static uint32_t       s_total_frames;
static uint32_t       s_ts_ms;
static uint32_t       s_dur_ms;
static uint32_t       s_inject_fail;
static char           s_name[16];
static uint8_t       *s_buf;      // 单帧读缓冲（PSRAM）
static uint32_t       s_buf_cap;

// 回放节拍基准：上一帧注入成功的时刻 + 那一帧的时间戳，用于算"还要等多久"
static int64_t  s_wall_us;
static uint32_t s_prev_ts;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

int player_list(player_clip_t *out, int max_items)
{
    if (out == NULL || max_items <= 0) {
        return 0;
    }
    DIR *d = opendir(REC_DIR);
    if (d == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max_items) {
        if (strncasecmp(e->d_name, "REC", 3) != 0) {
            continue;
        }
        uint32_t v = 0;
        int i = 3;
        while (e->d_name[i] >= '0' && e->d_name[i] <= '9') {
            v = v * 10 + (uint32_t)(e->d_name[i] - '0');
            ++i;
        }
        if (i == 3) {
            continue;
        }
        char full[288];
        snprintf(full, sizeof(full), REC_DIR "/%s", e->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || st.st_size < HDR_BYTES) {
            continue;           // 0 字节的残留文件不进列表
        }
        out[n].index = v;
        out[n].bytes = (uint32_t)st.st_size;
        snprintf(out[n].name, sizeof(out[n].name), "%.15s", e->d_name);  // %.15s：d_name 声明为 255 字节，不限精度会触发 -Werror=format-truncation
        ++n;
    }
    closedir(d);
    // 按序列号升序（条数很少，插入排序足够）
    for (int i = 1; i < n; ++i) {
        player_clip_t k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].index > k.index) {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = k;
    }
    return n;
}

// 扫一遍索引拿总帧数与总时长。只读 8 字节头再 seek 跳过数据，不搬 JPEG。
static void scan_index(FILE *f, uint32_t *frames, uint32_t *dur_ms)
{
    uint32_t n = 0, last_ts = 0;
    fseek(f, 0, SEEK_SET);
    for (;;) {
        uint8_t h[HDR_BYTES];
        if (fread(h, 1, HDR_BYTES, f) != HDR_BYTES) {
            break;
        }
        const uint32_t ts = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                            ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        const uint32_t ln = (uint32_t)h[4] | ((uint32_t)h[5] << 8) |
                            ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
        if (ln == 0 || ln > video_stream_max_frame_bytes()) {
            break;              // 文件被截断或损坏，索引到此为止
        }
        if (fseek(f, (long)ln, SEEK_CUR) != 0) {
            break;
        }
        last_ts = ts;
        ++n;
    }
    fseek(f, 0, SEEK_SET);
    *frames = n;
    *dur_ms = last_ts;
}

esp_err_t player_open(uint32_t index)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    player_clip_t clips[PLAYER_MAX_CLIPS];
    const int n = player_list(clips, PLAYER_MAX_CLIPS);
    if (n == 0) {
        ESP_LOGW(TAG, "卡上没有录像");
        return ESP_ERR_NOT_FOUND;
    }
    const player_clip_t *want = NULL;
    if (index == 0) {
        want = &clips[n - 1];           // 0 = 最后一片（刚录完就看）
    } else {
        for (int i = 0; i < n; ++i) {
            if (clips[i].index == index) {
                want = &clips[i];
                break;
            }
        }
    }
    if (want == NULL) {
        ESP_LOGW(TAG, "没有第 %lu 片", (unsigned long)index);
        return ESP_ERR_NOT_FOUND;
    }

    char full[288];
    snprintf(full, sizeof(full), REC_DIR "/%s", want->name);

    lock();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(full, "rb");
    if (s_fp == NULL) {
        unlock();
        ESP_LOGE(TAG, "打开 %s 失败", full);
        return ESP_FAIL;
    }
    scan_index(s_fp, &s_total_frames, &s_dur_ms);
    s_clip_index = want->index;
    snprintf(s_name, sizeof(s_name), "%.15s", want->name);
    s_frame = 0;
    s_ts_ms = 0;
    s_prev_ts = 0;
    s_inject_fail = 0;
    s_wall_us = esp_timer_get_time();
    s_state = PLAYER_PLAYING;
    unlock();

    video_stream_set_playback(true);
    ESP_LOGW(TAG, "回放 %s：%lu 帧 / %lu ms", s_name,
             (unsigned long)s_total_frames, (unsigned long)s_dur_ms);
    return ESP_OK;
}

void player_pause(void)
{
    if (s_lock == NULL) { return; }   // UI 可能在 player_init() 之前就被点到
    lock();
    if (s_state == PLAYER_PLAYING) {
        s_state = PLAYER_PAUSED;
        ESP_LOGW(TAG, "暂停在第 %lu/%lu 帧", (unsigned long)s_frame,
                 (unsigned long)s_total_frames);
    }
    unlock();
}

void player_resume(void)
{
    if (s_lock == NULL) { return; }
    lock();
    if (s_state == PLAYER_PAUSED) {
        s_state = PLAYER_PLAYING;
        s_wall_us = esp_timer_get_time();   // 重置节拍，别把暂停时长算成落后
        ESP_LOGW(TAG, "继续，从第 %lu 帧", (unsigned long)s_frame);
    }
    unlock();
}

esp_err_t player_seek(uint32_t frame)
{
    if (s_lock == NULL) { return ESP_ERR_INVALID_STATE; }
    lock();
    if (s_fp == NULL) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    // 变长帧只能顺序索引，但一帧只读 8 字节头 + seek，1000 帧也是毫秒级
    fseek(s_fp, 0, SEEK_SET);
    uint32_t i = 0, ts = 0;
    while (i < frame) {
        uint8_t h[HDR_BYTES];
        if (fread(h, 1, HDR_BYTES, s_fp) != HDR_BYTES) {
            break;
        }
        ts = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
             ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        const uint32_t ln = (uint32_t)h[4] | ((uint32_t)h[5] << 8) |
                            ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
        if (ln == 0 || ln > video_stream_max_frame_bytes()) {
            break;
        }
        if (fseek(s_fp, (long)ln, SEEK_CUR) != 0) {
            break;
        }
        ++i;
    }
    s_frame = i;
    s_ts_ms = ts;
    s_prev_ts = ts;
    s_wall_us = esp_timer_get_time();
    unlock();
    ESP_LOGW(TAG, "拖到第 %lu 帧 (ts=%lu ms)", (unsigned long)i, (unsigned long)ts);
    return ESP_OK;
}

void player_stop(void)
{
    if (s_lock == NULL) { return; }
    lock();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_state = PLAYER_IDLE;
    unlock();
    video_stream_set_playback(false);       // 放回实时画面
    ESP_LOGW(TAG, "停止回放，回到实时");
}

void player_get_status(player_status_t *out)
{
    if (out == NULL) {
        return;
    }
    // ⚠️ 这个守卫是必需的，不是防御性冗余：UI 的 100ms 定时器会一直调本函数，
    //    而 player_init() 若还没跑（或 SD 未挂载而 init 失败），s_lock 就是 NULL，
    //    直接 lock() 会命中 FreeRTOS 的 `assert failed: xQueueSemaphoreTake ... (pxQueue)`
    //    并把整机拖进重启循环 —— 2026-07-31 实测踩过，19 次重启。
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        out->state = PLAYER_IDLE;
        return;
    }
    lock();
    out->state        = s_state;
    out->clip_index   = s_clip_index;
    out->frame        = s_frame;
    out->total_frames = s_total_frames;
    out->ts_ms        = s_ts_ms;
    out->dur_ms       = s_dur_ms;
    out->inject_fail  = s_inject_fail;
    snprintf(out->name, sizeof(out->name), "%.15s", s_name);
    unlock();
}

static void player_task(void *arg)
{
    (void)arg;
    for (;;) {
        lock();
        if (s_state != PLAYER_PLAYING || s_fp == NULL) {
            unlock();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        uint8_t h[HDR_BYTES];
        if (fread(h, 1, HDR_BYTES, s_fp) != HDR_BYTES) {
            unlock();
            ESP_LOGW(TAG, "播完 %s（%lu 帧）", s_name, (unsigned long)s_frame);
            player_stop();
            continue;
        }
        const uint32_t ts = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                            ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
        const uint32_t ln = (uint32_t)h[4] | ((uint32_t)h[5] << 8) |
                            ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
        if (ln == 0 || ln > video_stream_max_frame_bytes()) {
            unlock();
            ESP_LOGE(TAG, "第 %lu 帧长度非法 %lu，停止", (unsigned long)s_frame,
                     (unsigned long)ln);
            player_stop();
            continue;
        }
        if (s_buf == NULL || s_buf_cap < ln) {
            heap_caps_free(s_buf);
            s_buf = heap_caps_malloc(ln, MALLOC_CAP_SPIRAM);
            s_buf_cap = (s_buf != NULL) ? ln : 0;
        }
        if (s_buf == NULL) {
            unlock();
            ESP_LOGE(TAG, "回放缓冲分配失败");
            player_stop();
            continue;
        }
        if (fread(s_buf, 1, ln, s_fp) != ln) {
            unlock();
            ESP_LOGW(TAG, "第 %lu 帧被截断，按播完处理", (unsigned long)s_frame);
            player_stop();
            continue;
        }
        // 原速：按相邻帧时间戳差，扣掉已经流逝的时间
        const uint32_t dt = (s_frame > 0 && ts > s_prev_ts) ? (ts - s_prev_ts) : 0;
        const int64_t elapsed_ms = (esp_timer_get_time() - s_wall_us) / 1000;
        int32_t rest = (int32_t)dt - (int32_t)elapsed_ms;
        s_prev_ts = ts;
        s_ts_ms = ts;
        unlock();

        if (rest > 0) {
            if (rest > 2000) {
                rest = 2000;        // 录像中断留下的大空档，别真的傻等
            }
            vTaskDelay(pdMS_TO_TICKS(rest));
        }
        // 注入失败 = 显示任务还没腾出槽，下一拍再试同一帧（不推进 s_frame）
        int tries = 0;
        while (!video_stream_inject_frame(s_buf, ln) && tries < 20) {
            ++tries;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        lock();
        if (tries > 0) {
            s_inject_fail += (uint32_t)tries;
        }
        s_wall_us = esp_timer_get_time();
        ++s_frame;
        unlock();
    }
}

esp_err_t player_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    if (!sdcard_info()->mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    // 优先级 3：与 recorder 写盘任务同级，都压在显示/RX 之下
    if (xTaskCreate(player_task, "player", 4096, NULL, 3, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "回放模块就绪");
    return ESP_OK;
}
