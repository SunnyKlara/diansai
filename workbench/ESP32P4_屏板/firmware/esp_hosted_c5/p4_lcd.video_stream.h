// -*- coding: utf-8 -*-
// 方案 D 最后一段：把「无线收帧」和「硬件解码上屏」合到一个固件里。
//
// 本文件是两条已各自真机验证过的路径的拼装，不是新探索：
//   收帧  ← p4_sta_host/main/main.c 的 video_client_task（K230 当 AP+server，
//           P4 当 STA+client；1025 帧零坏帧、51.23fps / 3.10Mbps，README §10.10）
//   解码  ← p4_lcd/main/jpeg_view.c（P4 硬件 JPEG 3.31ms/帧≈302fps，README §10.8）
// 差别只在于：帧数据源从「固件里嵌的那一张」换成 socket 收下来的连续帧，
// 且解码引擎/缓冲改成建一次复用（原来是一次性的）。
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
    uint32_t frames;        // 开机以来累计收到的帧
    uint32_t bad;           // 丢弃帧数（magic 错 / 超长 / JPEG 头尾不对）
    uint32_t decode_fail;   // 解码失败次数
    uint32_t reconnects;    // TCP 重连次数
    uint32_t fps_x10;       // 最近 1s 帧率 ×10
    uint32_t mbps_x100;     // 最近 1s 码率 ×100
    uint32_t last_len;      // 最近一帧 JPEG 字节数
    uint32_t dec_us;        // 最近一帧硬件解码耗时（µs）
    char     note[56];      // 一行人可读状态，直接打在屏上
} video_stats_t;

// 启动无线图传：内部起一个任务做 NVS → WiFi STA(经 SDIO 到 C5) → 可选 ping →
// TCP 拉流 → 硬件解码 → 把解好的 RGB565 缓冲挂到 canvas 上。
//
// canvas 必须是已创建好的 lv_canvas 对象（由 ai_panel 建）。任务自己会在
// lvgl_port_lock 内调用 lv_canvas_set_buffer，因此调用方不需要额外加锁。
// 传 NULL 表示只收帧+解码不上屏（用于把「上屏」这一变量摘掉做对照）。
void video_stream_start(lv_obj_t *canvas);

// 拷一份当前指标出来（供 LVGL 定时器刷标签）。
void video_stream_get_stats(video_stats_t *out);

#ifdef __cplusplus
}
#endif
