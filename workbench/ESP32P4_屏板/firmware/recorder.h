// -*- coding: utf-8 -*-
// MJPEG 录像 / 回放（P4 侧存卡）。
//
// 前置事实（2026-07-30 真机，详见 workbench/ESP32P4_屏板/README.md §10.14）：
//   · SD 卡能用的前提是 sdcard.c 里那句片上 LDO_VO4 供电（P4 的 SDMMC IO 域外部供电）
//   · 挂载的是 MBR 第 3 个分区（p1 只有 10MB 且放着 GC2093 标定文件）⇒ 58606 MB 可用
//   · **稳态顺序写实测 415 KB/s**，而图传码率约 460 KB/s ⇒ **全帧率录不下来**，
//     所以本模块默认抽帧（REC_EVERY_N）。原速回放靠文件里的时间戳，抽帧不影响原速。
//
// 文件格式（与 K230 侧 k230_recplay.py 一致，两边可互读）：
//   每帧： uint32 LE 相对时间戳(ms) | uint32 LE JPEG 长度 | JPEG 字节
//   时间戳相对**本次录像第一帧**，回放时按相邻差值延时即得原速。
//   为什么不塞进 TCP 帧头：那会改动已真机 PASS 的图传协议，没必要。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// 每 N 帧存 1 帧。N=1 表示全存（**实测带宽不够，会大量丢帧**）。
// 图传约 57 fps ⇒ N=4 约 14 fps、约 115 KB/s，相对 415 KB/s 写速有 3.6 倍余量。
#ifndef REC_EVERY_N
#define REC_EVERY_N 4
#endif

// 剩余空间低于这个就停录（留余量给 FAT 元数据，别把卡写到一点不剩）
#ifndef REC_MIN_FREE_MB
#define REC_MIN_FREE_MB 64
#endif

typedef struct {
    bool     recording;
    uint32_t frames;        // 已写入的帧数
    uint32_t dropped;       // 因队列满而丢弃的帧（写不过来的量化指标）
    uint32_t skipped;       // 抽帧策略主动跳过的帧
    uint32_t kbytes;        // 已写入 KB
    uint32_t ms;            // 已录时长
    char     path[40];      // 当前/最近一个文件
} recorder_stats_t;

// 建队列 + 起写盘任务。SD 未挂载时返回 ESP_ERR_INVALID_STATE。
esp_err_t recorder_init(void);

// 开始/停止。start 会按序列号新建 /sdcard/REC%05d.MJP（扫描现有最大号 +1，
// 所以删掉中间文件也不会撞名）。
esp_err_t recorder_start(void);
void      recorder_stop(void);
bool      recorder_is_recording(void);

// 由 RX 任务调用：把一帧交给录像。**非阻塞** —— 内部拷贝一份进队列，队列满就丢并计数，
// 绝不让写盘拖慢网络接收。抽帧判断也在这里做。
void recorder_feed(const uint8_t *jpg, uint32_t len);

void recorder_get_stats(recorder_stats_t *out);
