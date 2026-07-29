// -*- coding: utf-8 -*-
// 2026-07-28 真机机器侧验证：3 输入槽 latest-frame 解耦流水线完成 180 s 长跑，
// rx=10193、shown=2981、drop=7212、bad=0、decode_fail=0；屏幕观感人眼 PASS（含固定中线已修）。
// 剩「画面区轻微闪烁」待定因，见 workbench/ESP32P4_屏板/README.md §10.12 末。
//
// 帧格式（与 K230 侧 k230_ap_stream.py 一致）：'J' 'F' | uint32 LE 长度 | JPEG 字节
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 屏上要显示的定量指标。由 video 任务写、LVGL 定时器读；两边不加锁，
// 最坏情况是某一帧读到半新半旧的数字（100ms 后自愈），不值得为它上锁。
typedef struct {
    bool     link_up;       // 关联成功且拿到 IP
    bool     stream_up;     // TCP 已连上且正在收帧
    char     ip[16];        // 本机 IP（DHCP 或静态兜底）
    char     gw[16];        // 网关 = K230，也是视频服务端地址
    uint32_t frames;        // 开机以来累计收到且通过 JPEG marker 校验的帧
    uint32_t shown;         // 成功解码并完成 canvas 换缓冲的帧
    uint32_t dropped;       // latest 队列中未消费旧帧被新帧替换的次数
    uint32_t bad;           // 坏帧数（magic 错 / 长度非法 / JPEG 头尾不对）
    uint32_t decode_fail;   // 硬解码失败次数
    uint32_t reconnects;    // TCP 重连次数
    uint32_t fps_x10;       // 最近约 1s 接收帧率 ×10
    uint32_t show_fps_x10;  // 最近约 1s 成功上屏帧率 ×10
    uint32_t mbps_x100;     // 最近约 1s 接收码率 ×100
    uint32_t last_len;      // 最近一帧 JPEG 字节数
    uint32_t dec_us;        // 最近一帧硬件解码耗时（µs）
    char     note[56];      // 一行人可读状态，直接打在屏上
} video_stats_t;

// 启动无线图传：内部建立 3 个 JPEG DMA 输入槽、容量 1 的 latest 队列和独立
// RX/显示任务，再完成 NVS → WiFi STA(经 SDIO 到 C5) → 可选 ping → TCP 拉流。
// RX 只保留最新未消费帧；显示任务硬解码后在 LVGL 锁内切换 RGB565 双缓冲。
//
// canvas 必须是已创建好的 lv_canvas 对象（由 ai_panel 建）。传 NULL 表示收帧并
// 解码但不上屏，用于把 canvas 这一变量摘掉做对照。
void video_stream_start(lv_obj_t *canvas);

// 拷一份当前指标出来（供 LVGL 定时器刷标签）。
void video_stream_get_stats(video_stats_t *out);

#ifdef __cplusplus
}
#endif
