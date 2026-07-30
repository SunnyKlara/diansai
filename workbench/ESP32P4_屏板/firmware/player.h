// -*- coding: utf-8 -*-
// 录像回放：列片 / 播放 / 暂停 / 拖动 / 原速。
//
// 读的是 recorder.c 写的文件（`/sdcard/REC%05d.MJP`），格式：
//   每帧： uint32 LE 相对时间戳(ms) | uint32 LE JPEG 长度 | JPEG 字节
// **原速**靠相邻帧时间戳之差延时得到 —— 所以录像端抽帧（1/4）不影响回放速度，
// 这也是当初把时间戳放进文件、而不是塞进 TCP 帧头的原因。
//
// 显示路径不另建：调 video_stream_inject_frame()，走与网络帧完全相同的
// slot → latest 队列 → 硬解码 → LVGL canvas，那条路已经真机验过。
// 回放期间 video_stream_set_playback(true) 让网络帧照收但不上屏。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define PLAYER_MAX_CLIPS 16

typedef struct {
    uint32_t index;         // 文件序列号（REC00007.MJP -> 7）
    uint32_t bytes;         // 文件大小
    char     name[16];      // "REC00007.MJP"
} player_clip_t;

typedef enum {
    PLAYER_IDLE = 0,        // 没在回放（实时画面）
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state_t;

typedef struct {
    player_state_t state;
    uint32_t clip_index;    // 正在放哪一片
    uint32_t frame;         // 当前帧序号（从 0 起）
    uint32_t total_frames;  // 该片总帧数（打开时扫一遍索引得到）
    uint32_t ts_ms;         // 当前帧时间戳
    uint32_t dur_ms;        // 该片总时长
    uint32_t inject_fail;   // 因取不到显示槽而重试的次数（诊断用）
    char     name[16];
} player_status_t;

// 建任务。SD 未挂载时返回 ESP_ERR_INVALID_STATE。
esp_err_t player_init(void);

// 列出卡上的录像片（按序列号升序）。返回条数，最多 PLAYER_MAX_CLIPS。
int player_list(player_clip_t *out, int max_items);

// 开始回放。index=0 表示"最后一片"（最常用：刚录完就看）。
esp_err_t player_open(uint32_t index);

void player_pause(void);
void player_resume(void);

// 拖动：跳到第 frame 帧。文件是变长帧，只能顺序 skip 索引，
// 但一帧只需读 8 字节头 + seek 跳过数据，1000 帧也是毫秒级。
esp_err_t player_seek(uint32_t frame);

// 停止回放并回到实时画面。
void player_stop(void);

void player_get_status(player_status_t *out);
