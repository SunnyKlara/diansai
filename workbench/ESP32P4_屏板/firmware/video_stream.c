// -*- coding: utf-8 -*-
// 2026-07-28 真机验证：latest-frame 流水线 180 s 收 10193 帧、零坏帧/解码失败；屏幕观感人眼 PASS
// （实时/颜色/方向正常；曾有的固定中线与跨线重叠已由 buffer_px 扩为整屏修掉并复验）。
// 剩一个独立症状 待定因：画面区轻微闪烁（候选=下层控件跨进画面区 / 无 VSYNC / 上屏仅约 14~17 FPS）。
// 注释里的数字凡标 MEASURED 的都是本板真机实测，其余标 待验证。

#include "recorder.h"
#include "video_stream.h"

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "driver/jpeg_decode.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

static const char *TAG = "video";

// ---- 链路参数 ---------------------------------------------------------------
// 拓扑 C：K230 = AP + TCP server（网关 192.168.169.1），P4 = STA + client。
// 为什么不是 P4 当 AP：P4/C5 当 SoftAP 时它每 ~15.58s 打一对
// WIFI_EVENT_HOME_CHANNEL_CHANGE（自己离开自己的信道），K230 connect() 得到
// ENOTCONN；角色翻过来后 P4 关联 + DHCP + ping 5/5 全通（README §10.10）。
#define VIDEO_PORT        5001
#define DHCP_WAIT_MS      12000
#define VIDEO_RX_TIMEO_S  10

// MEASURED：K230 640x480 硬件 JPEG 单帧 6608~7239B，但见过 58315B 的峰值帧
// （画面剧烈变化时）。每个槽预留 96KB，直接 recv 进 DMA 缓冲，省一次拷贝。
// 三槽分别允许「接收中 / 等待显示的 latest / 正在解码」同时存在；两槽会在
// 消费者持一槽、latest 队列占一槽时迫使接收端停下来，无法真正解除 TCP 反压。
#define FRAME_MAX_BYTES   (96 * 1024)
#define RX_SLOT_COUNT     3

// 解码输出按 640x480 RGB565 预留两块（双缓冲）。K230 侧就是这个分辨率；
// 若 JPEG 头部报出更大的尺寸，本文件选择「丢帧并报错」而不是越界写。
#define DEC_MAX_W         640
#define DEC_MAX_H         480
#define DEC_OUT_BYTES     ((size_t)DEC_MAX_W * DEC_MAX_H * 2)

// ---- WiFi 状态 -------------------------------------------------------------
#define BIT_GOT_IP      BIT0
#define BIT_ASSOCIATED  BIT1

static EventGroupHandle_t s_wifi_ev;
static esp_netif_t       *s_sta_netif;
static esp_ip4_addr_t     s_gw_ip;
static int                s_retry;

static video_stats_t s_st;
static lv_obj_t     *s_canvas;

// 接收与显示解耦：生产者从 s_free_slots 取槽，收完后投递到容量为 1 的
// s_latest_frame；若已有未消费帧，先回收旧槽并计 dropped，再放最新帧。
// 消费者拿走槽后独占到解码结束，最后归还 free 队列，绝不发生并发覆写。
typedef struct {
    uint8_t *data;
    size_t   alloc;
    uint32_t len;
} rx_slot_t;

static rx_slot_t   s_rx[RX_SLOT_COUNT];
static QueueHandle_t s_free_slots;
static QueueHandle_t s_latest_frame;

// 解码资源：建一次复用。jpeg_view.c 那版是一次性的（建引擎→解一帧→删引擎），
// 每帧重建引擎在 50fps 下纯属浪费。
static jpeg_decoder_handle_t s_dec;
static uint8_t *s_out[2];             // 双缓冲输出，LVGL 显示一块、解码写另一块
static size_t   s_out_alloc;
static int      s_wr;                 // 下一帧写哪块

void video_stream_get_stats(video_stats_t *out)
{
    if (out) {
        *out = s_st;
    }
}

static void set_note(const char *s)
{
    strncpy(s_st.note, s, sizeof(s_st.note) - 1);
    s_st.note[sizeof(s_st.note) - 1] = '\0';
}

// ============================ WiFi ==========================================
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        // 把「关联上了」和「拿到 IP 了」分开记：DHCP 失败与关联失败是两种病，
        // 混在一起看会往错的方向查（第一版就吃过这个亏）。
        ESP_LOGI(TAG, "STA_CONNECTED (associated, waiting for DHCP)");
        set_note("associated, waiting DHCP");
        xEventGroupSetBits(s_wifi_ev, BIT_ASSOCIATED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_st.link_up = false;
        s_st.stream_up = false;
        // 永不彻底放弃重连。p4_sta_host 那版是「重试 N 次后置 FAIL 位就收手」——
        // 那是给一次性验收脚本用的。合进屏固件后行为要求不同：K230 完全可能比 P4
        // 后开机 / 中途重启，固件必须自己等到它出现，而不是要求人重新上电 P4。
        s_retry++;
        if (s_retry <= 3 || s_retry % 10 == 0) {
            ESP_LOGI(TAG, "disconnected (attempt %d), reconnecting", s_retry);
        }
        snprintf(s_st.note, sizeof(s_st.note), "waiting for %s (try %d)",
                 CONFIG_P4V_WIFI_SSID, s_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip:" IPSTR " gw:" IPSTR, IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        s_gw_ip = e->ip_info.gw;
        snprintf(s_st.ip, sizeof(s_st.ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(s_st.gw, sizeof(s_st.gw), IPSTR, IP2STR(&e->ip_info.gw));
        s_retry = 0;
        s_st.link_up = true;
        xEventGroupSetBits(s_wifi_ev, BIT_GOT_IP);
    }
}

static bool wifi_up(void)
{
    s_wifi_ev = xEventGroupCreate();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    // p4_lcd 里别的模块理论上也可能建过默认 loop，已存在就当成功。
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
        return false;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // 这一行才是真正跨 SDIO 去把 C5 拉起来的地方（esp_wifi -> esp_wifi_remote
    // -> esp_hosted），耗时以秒计，所以本模块整体放在 LVGL 起来之后跑。
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s (C5 co-processor not answering over SDIO?)",
                 esp_err_to_name(err));
        set_note("esp_wifi_init FAILED");
        return false;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_P4V_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_P4V_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    // PMF 两个标志都要关：只关 required 不够（实测 K230 侧 AP 不带 PMF）。
    wc.sta.pmf_cfg.capable  = false;
    wc.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    set_note("joining " CONFIG_P4V_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    // 刻意不打印 PSK：串口日志会被抓下来贴进文档/归档。
    ESP_LOGI(TAG, "wifi sta started, joining SSID:%s", CONFIG_P4V_WIFI_SSID);

    // 等 IP，等不到就一直等（同上：AP 可能还没开机）。每 DHCP_WAIT_MS 报一次
    // 「卡在哪个阶段」——关联不上 和 关联上了但没租约，是两种病。
    for (int round = 1; ; round++) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_ev, BIT_GOT_IP, pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(DHCP_WAIT_MS));
        if (bits & BIT_GOT_IP) {
            return true;
        }
        if (xEventGroupGetBits(s_wifi_ev) & BIT_ASSOCIATED) {
            ESP_LOGW(TAG, "associated but no DHCP lease after %d ms (round %d)", DHCP_WAIT_MS, round);
            set_note("associated, NO DHCP lease");
        } else {
            ESP_LOGW(TAG, "not associated to %s yet (round %d) -- is the AP up?",
                     CONFIG_P4V_WIFI_SSID, round);
        }
    }
}

#if CONFIG_P4V_PING_ON_BOOT
static EventGroupHandle_t s_ping_done;
#define BIT_PING_DONE BIT0

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t tx = 0, rx = 0, ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &ms, sizeof(ms));
    // 单行可 grep 的判据，和 p4_sta_host 里那条格式一致，方便横向对账。
    ESP_LOGI(TAG, "PING SUMMARY tx=%lu rx=%lu loss=%lu%% duration=%lu ms -> %s",
             (unsigned long)tx, (unsigned long)rx,
             (unsigned long)(tx ? (tx - rx) * 100 / tx : 100), (unsigned long)ms,
             rx > 0 ? "PING_OK" : "PING_FAIL");
    xEventGroupSetBits(s_ping_done, BIT_PING_DONE);
}

static void ping_gw(void)
{
    if (s_gw_ip.addr == 0) {
        return;
    }
    s_ping_done = xEventGroupCreate();
    ip_addr_t target = { 0 };
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = s_gw_ip.addr;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = 3;                       // 3 个够判「通不通」；开机就别磨 5 个了
    esp_ping_callbacks_t cbs = { .on_ping_end = on_ping_end };
    esp_ping_handle_t p = NULL;
    if (esp_ping_new_session(&cfg, &cbs, &p) != ESP_OK) {
        ESP_LOGW(TAG, "PING SUMMARY skipped -> PING_FAIL (session create failed)");
        return;
    }
    set_note("pinging gateway");
    esp_ping_start(p);
    xEventGroupWaitBits(s_ping_done, BIT_PING_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(10000));
    esp_ping_stop(p);
    esp_ping_delete_session(p);
}
#endif  // CONFIG_P4V_PING_ON_BOOT

// ============================ 硬件 JPEG 解码 =================================
static bool decoder_setup(void)
{
    // 40ms 超时：远高于 30fps 的帧间隔，所以真超时就是「解码器卡住了」而不是
    // 「我们太急」。实测单帧 3.31ms。
    jpeg_decode_engine_cfg_t eng = { .intr_priority = 0, .timeout_ms = 40 };
    esp_err_t err = jpeg_new_decoder_engine(&eng, &s_dec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "jpeg engine: %s", esp_err_to_name(err));
        set_note("jpeg engine FAILED");
        return false;
    }

    // 输入/输出都必须是 DMA 可达且对齐的缓冲，只有这个 helper 能保证；
    // 普通 malloc 出来的指针不能当码流缓冲用。
    jpeg_decode_memory_alloc_cfg_t in_cfg  = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };

    bool input_ok = true;
    for (int i = 0; i < RX_SLOT_COUNT; i++) {
        s_rx[i].data = jpeg_alloc_decoder_mem(FRAME_MAX_BYTES, &in_cfg, &s_rx[i].alloc);
        input_ok = input_ok && (s_rx[i].data != NULL);
    }
    for (int i = 0; i < 2; i++) {
        s_out[i] = jpeg_alloc_decoder_mem(DEC_OUT_BYTES, &out_cfg, &s_out_alloc);
    }
    if (!input_ok || !s_out[0] || !s_out[1]) {
        ESP_LOGE(TAG, "jpeg buffers: rx=%u/%u/%u out=%u (need 3x%u + 2x%u)",
                 (unsigned)s_rx[0].alloc, (unsigned)s_rx[1].alloc,
                 (unsigned)s_rx[2].alloc, (unsigned)s_out_alloc,
                 (unsigned)FRAME_MAX_BYTES, (unsigned)DEC_OUT_BYTES);
        set_note("jpeg buffer alloc FAILED");
        return false;
    }

    s_free_slots = xQueueCreate(RX_SLOT_COUNT, sizeof(uint8_t));
    s_latest_frame = xQueueCreate(1, sizeof(uint8_t));
    if (!s_free_slots || !s_latest_frame) {
        ESP_LOGE(TAG, "frame queue alloc failed");
        set_note("frame queue alloc FAILED");
        return false;
    }
    for (uint8_t i = 0; i < RX_SLOT_COUNT; i++) {
        if (xQueueSend(s_free_slots, &i, 0) != pdTRUE) {
            ESP_LOGE(TAG, "failed to seed free slot %u", (unsigned)i);
            set_note("frame queue seed FAILED");
            return false;
        }
    }

    ESP_LOGI(TAG, "decoder ready: rx 3 x %u B, out 2 x %u B, latest depth 1",
             (unsigned)s_rx[0].alloc, (unsigned)s_out_alloc);
    return true;
}

// 解一帧到 s_out[idx]，成功则把该缓冲挂上 canvas。
static bool decode_and_show(const uint8_t *jpeg, uint32_t len)
{
    jpeg_decode_picture_info_t info = { 0 };
    esp_err_t err = jpeg_decoder_get_info(jpeg, len, &info);
    if (err != ESP_OK) {
        s_st.decode_fail++;
        return false;
    }
    if (info.width > DEC_MAX_W || info.height > DEC_MAX_H) {
        ESP_LOGE(TAG, "frame %lux%lu exceeds the %dx%d buffer -- lower the K230 resolution "
                      "or raise DEC_MAX_W/H",
                 (unsigned long)info.width, (unsigned long)info.height, DEC_MAX_W, DEC_MAX_H);
        s_st.decode_fail++;
        return false;
    }

    const int idx = s_wr;
    jpeg_decode_cfg_t cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        // 红蓝互换就把这里改成 _RGB。屏上看到的第一版就是用 _BGR 出的。
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    const int64_t t0 = esp_timer_get_time();
    err = jpeg_decoder_process(s_dec, &cfg, jpeg, len, s_out[idx],
                               (uint32_t)s_out_alloc, &out_len);
    s_st.dec_us = (uint32_t)(esp_timer_get_time() - t0);
    if (err != ESP_OK) {
        s_st.decode_fail++;
        if (s_st.decode_fail < 5) {
            ESP_LOGE(TAG, "decode failed: %s (frame %lu B)", esp_err_to_name(err), (unsigned long)len);
        }
        return false;
    }

    if (s_canvas) {
        // lvgl_port 的渲染任务在整个 lv_timer_handler 期间持这把锁，所以锁内换
        // 缓冲不会撞上正在进行的渲染；而解码是在锁外写「另一块」缓冲，两者不重叠。
        // 用 lv_canvas 而不是 lv_image：lv_canvas_set_buffer 自己会把旧 src 的
        // 图像缓存 drop 掉，省得自己去猜 LVGL 缓存什么时候会喂回旧数据。
        if (lvgl_port_lock(200)) {
            lv_canvas_set_buffer(s_canvas, s_out[idx], (int32_t)info.width,
                                 (int32_t)info.height, LV_COLOR_FORMAT_RGB565);
            lvgl_port_unlock();
        } else {
            // 拿不到锁就别换手，宁可这一帧不上屏（也别去动 LVGL 正在读的缓冲）
            ESP_LOGW(TAG, "lvgl lock timeout, frame not displayed");
            return false;
        }
    }

    // 换手只发生在成功挂屏（或无 canvas 对照模式）之后：下一次解码写旧显示缓冲，
    // 当前 canvas 所指缓冲在解码期间保持只读。
    s_wr ^= 1;
    s_st.shown++;
    return true;
}

static void display_task(void *arg)
{
    (void)arg;
    uint32_t win_shown = 0;
    int64_t t_win = esp_timer_get_time();

    while (1) {
        uint8_t slot_idx;
        const BaseType_t got = xQueueReceive(s_latest_frame, &slot_idx, pdMS_TO_TICKS(1000));
        if (got == pdTRUE) {
            rx_slot_t *slot = &s_rx[slot_idx];
            if (decode_and_show(slot->data, slot->len)) {
                win_shown++;
            }
            // 解码完成后才归还；生产者不可能覆写正在被硬解码器读取的输入。
            if (xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "free-slot queue invariant broken for slot %u", (unsigned)slot_idx);
            }
        }

        const int64_t now = esp_timer_get_time();
        if (now - t_win >= 1000000) {
            const uint32_t ms = (uint32_t)((now - t_win) / 1000);
            s_st.show_fps_x10 = win_shown * 10000UL / ms;
            ESP_LOGI(TAG, "SHOW %lu.%lu fps | shown %lu drop %lu | decode %lu us fail %lu",
                     (unsigned long)(s_st.show_fps_x10 / 10),
                     (unsigned long)(s_st.show_fps_x10 % 10),
                     (unsigned long)s_st.shown, (unsigned long)s_st.dropped,
                     (unsigned long)s_st.dec_us, (unsigned long)s_st.decode_fail);
            win_shown = 0;
            t_win = now;
        }
    }
}

// ============================ 回放支路 ======================================
static volatile bool s_playback;

void video_stream_set_playback(bool on)
{
    s_playback = on;
    set_note(on ? "playback" : "streaming (latest-frame)");
}

bool video_stream_is_playback(void) { return s_playback; }

uint32_t video_stream_max_frame_bytes(void) { return (uint32_t)FRAME_MAX_BYTES; }

bool video_stream_inject_frame(const uint8_t *jpg, uint32_t len)
{
    if (jpg == NULL || len < 4 || len > (uint32_t)FRAME_MAX_BYTES) {
        return false;
    }
    if (s_free_slots == NULL || s_latest_frame == NULL) {
        return false;       // 解码器还没起来
    }
    uint8_t idx;
    // 不阻塞：拿不到槽就让调用方下一拍再试，避免回放任务把显示任务饿死
    if (xQueueReceive(s_free_slots, &idx, 0) != pdTRUE) {
        return false;
    }
    rx_slot_t *slot = &s_rx[idx];
    if (len > slot->alloc) {
        xQueueSend(s_free_slots, &idx, portMAX_DELAY);
        return false;
    }
    memcpy(slot->data, jpg, len);
    slot->len = len;

    // 与网络路径同样的"只留最新帧"语义
    uint8_t old_idx;
    if (xQueueReceive(s_latest_frame, &old_idx, 0) == pdTRUE) {
        s_st.dropped++;
        xQueueSend(s_free_slots, &old_idx, portMAX_DELAY);
    }
    if (xQueueSend(s_latest_frame, &idx, 0) != pdTRUE) {
        xQueueSend(s_free_slots, &idx, portMAX_DELAY);
        return false;
    }
    s_st.last_len = len;
    return true;
}

// ============================ TCP 拉流 ======================================
static bool rx_exact(int sock, uint8_t *dst, size_t want)
{
    size_t got = 0;
    while (got < want) {
        int n = recv(sock, dst + got, want - got, 0);
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    return true;
}

static void pull_frames_forever(void)
{
    char gw[16];
    snprintf(gw, sizeof(gw), IPSTR, IP2STR(&s_gw_ip));

    while (1) {
        int cs = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (cs < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct sockaddr_in a = { 0 };
        a.sin_family      = AF_INET;
        a.sin_port        = htons(VIDEO_PORT);
        a.sin_addr.s_addr = inet_addr(gw);
        ESP_LOGI(TAG, "dialing %s:%d", gw, VIDEO_PORT);
        set_note("dialing K230:5001");
        if (connect(cs, (struct sockaddr *)&a, sizeof(a)) != 0) {
            ESP_LOGW(TAG, "connect failed errno=%d, retrying", errno);
            set_note("K230 server not up yet");
            close(cs);
            s_st.reconnects++;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        int one = 1;
        setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        struct timeval rtv = { .tv_sec = VIDEO_RX_TIMEO_S, .tv_usec = 0 };
        setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
        ESP_LOGI(TAG, "connected -- RX producer running");
        s_st.stream_up = true;
        set_note("streaming (latest-frame)");

        uint32_t win_frames = 0;
        uint64_t win_bytes = 0;
        int64_t  t_win = esp_timer_get_time();
        uint32_t link_frames = 0;
        bool     paused = false;      // K230 是否处于"停发"态（由 len==0 心跳判定）

        while (1) {
            uint8_t hdr[6];
            if (!rx_exact(cs, hdr, sizeof(hdr))) {
                break;
            }
            // 'JM' = K230 侧的元数据/应答帧（REC/PLAY/LIST/STAT/ERR 文本行，
            // 见 firmware/esp_hosted_c5/k230_recplay.py）。它不是图像，不能进 JPEG
            // 解码流水线，所以在这里单独消费掉：打日志 + 把首段贴到屏上的 note。
            // ⚠️ 必须放在 'JF' 判定之前 —— 否则 K230 每发一次状态就被判 bad magic 而断线
            // 重连，症状看起来像"网络不稳"，是那种极难往协议上想的坑。
            if (hdr[0] == 'J' && hdr[1] == 'M') {
                const uint32_t mlen = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) |
                                      ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);
                char     meta[192];
                bool     meta_ok = true;
                uint32_t got = 0;
                while (got < mlen) {
                    uint32_t chunk = mlen - got;
                    if (chunk > sizeof(meta) - 1) {
                        chunk = sizeof(meta) - 1;
                    }
                    if (!rx_exact(cs, (uint8_t *)meta, chunk)) {
                        meta_ok = false;
                        break;
                    }
                    meta[chunk] = '\0';
                    if (got == 0) {
                        ESP_LOGI(TAG, "K230 meta: %s", meta);
                        set_note(meta);          // 内部 strncpy 截断，安全
                    } else {
                        ESP_LOGI(TAG, "K230 meta(cont): %s", meta);
                    }
                    got += chunk;
                }
                if (!meta_ok) {
                    break;
                }
                continue;
            }
            if (hdr[0] != 'J' || hdr[1] != 'F') {
                if (++s_st.bad < 5) {
                    ESP_LOGW(TAG, "bad magic %02x %02x -- reconnecting to resync", hdr[0], hdr[1]);
                }
                // magic 已错就不知道下一个帧边界在哪；继续按 6 字节切只会永久错位。
                break;
            }
            const uint32_t len = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) |
                                 ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);

            // len==0 = K230 侧的"已暂停"心跳（USER 键停发期间每 2 s 一个）。
            // 为什么需要它：本函数把 SO_RCVTIMEO 设成 VIDEO_RX_TIMEO_S=10 s，
            // 而"保持连接、只停发"意味着可能几分钟不发图 —— 没有心跳会被判超时断连，
            // 重连又会让 K230 的 accept 循环重来一遍。心跳是 6 字节纯帧头、无 payload。
            // ⚠️ 必须在取 slot 之前特判：它没有数据，占一个 slot 纯属白占；
            //    也必须放在 len<4 检查之前，否则会被当成非法长度而断线。
            if (len == 0) {
                if (!paused) {
                    paused = true;
                    set_note("paused (K230 USER key)");
                }
                continue;
            }
            if (paused) {
                paused = false;
                set_note("streaming (latest-frame)");
            }

            uint8_t slot_idx;
            if (xQueueReceive(s_free_slots, &slot_idx, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "free-slot queue unavailable");
                break;
            }
            rx_slot_t *slot = &s_rx[slot_idx];
            if (len < 4 || len > slot->alloc) {
                ESP_LOGE(TAG, "invalid frame length %lu for slot %u B -- reconnecting",
                         (unsigned long)len, (unsigned)slot->alloc);
                s_st.bad++;
                xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY);
                break;                    // 长度不可信 => 流已错位，重连比硬啃安全
            }
            if (!rx_exact(cs, slot->data, len)) {
                xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY);
                break;
            }
            const bool markers_ok = (slot->data[0] == 0xFF && slot->data[1] == 0xD8 &&
                                     slot->data[len - 2] == 0xFF && slot->data[len - 1] == 0xD9);
            if (!markers_ok) {
                s_st.bad++;
                xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY);
                continue;
            }
            slot->len = len;
            if (link_frames == 0) {
                ESP_LOGI(TAG, "first frame %lu B, markers OK", (unsigned long)len);
            }

            // 录像旁路：内部自己抽帧 + 拷贝 + 非阻塞入队，写盘在独立任务里做。
            // 放在 markers 校验之后 ⇒ 只录完整帧；不占 rx_slot ⇒ 写盘慢也不会反压网络。
            recorder_feed(slot->data, len);

            // 回放中：网络帧照收（保持连接与统计），但不上屏，否则会盖掉回放画面
            if (s_playback) {
                s_st.dropped++;
                xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY);
                link_frames++;
                s_st.frames++;
                continue;
            }

            // 容量 1 的 latest 队列：未消费旧帧没有显示价值，回收它并只保留最新帧。
            uint8_t old_idx;
            if (xQueueReceive(s_latest_frame, &old_idx, 0) == pdTRUE) {
                s_st.dropped++;
                xQueueSend(s_free_slots, &old_idx, portMAX_DELAY);
            }
            if (xQueueSend(s_latest_frame, &slot_idx, 0) != pdTRUE) {
                // 单生产者下理论上不应发生；仍然回收当前槽，避免所有权泄漏。
                s_st.dropped++;
                xQueueSend(s_free_slots, &slot_idx, portMAX_DELAY);
                ESP_LOGE(TAG, "latest queue invariant broken, frame dropped");
            }

            s_st.last_len = len;
            s_st.frames++;
            link_frames++;
            win_frames++;
            win_bytes += len;

            const int64_t now = esp_timer_get_time();
            if (now - t_win >= 1000000) {
                const uint32_t ms = (uint32_t)((now - t_win) / 1000);
                // 码率：bits/ms = kbit/s，所以 Mbps×100 = bytes*8/(ms*10)。
                // 第一版写成 bytes*800/ms/10，大了 100 倍、打出「440 Mbps」这种
                // 物理上不可能的数——靠发送端独立报的 3.12Mbps 才发现。
                // 同一个量必须有两个独立测量。
                s_st.fps_x10   = win_frames * 10000UL / ms;
                s_st.mbps_x100 = (uint32_t)((win_bytes * 8ULL) / (10ULL * ms));
                ESP_LOGI(TAG, "RX %lu.%lu fps | %lu.%02lu Mbps | frame %lu B | "
                              "rx %lu shown %lu drop %lu bad %lu",
                         (unsigned long)(s_st.fps_x10 / 10), (unsigned long)(s_st.fps_x10 % 10),
                         (unsigned long)(s_st.mbps_x100 / 100), (unsigned long)(s_st.mbps_x100 % 100),
                         (unsigned long)s_st.last_len, (unsigned long)s_st.frames,
                         (unsigned long)s_st.shown, (unsigned long)s_st.dropped,
                         (unsigned long)s_st.bad);
                win_frames = 0;
                win_bytes = 0;
                t_win = now;
            }
        }
        ESP_LOGW(TAG, "link closed after %lu RX frames (total %lu, shown %lu, drop %lu, bad %lu)",
                 (unsigned long)link_frames, (unsigned long)s_st.frames,
                 (unsigned long)s_st.shown, (unsigned long)s_st.dropped,
                 (unsigned long)s_st.bad);
        s_st.stream_up = false;
        s_st.fps_x10 = 0;
        s_st.mbps_x100 = 0;
        set_note("link closed, redialing");
        close(cs);
        s_st.reconnects++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ============================ 任务 ==========================================
static void video_task(void *arg)
{
    (void)arg;
    set_note("nvs init");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (!decoder_setup()) {
        vTaskDelete(NULL);
        return;
    }
    set_note("wifi init (C5 over SDIO)");
    if (!wifi_up()) {
        ESP_LOGE(TAG, "no link -- video not started");
        vTaskDelete(NULL);
        return;
    }
#if CONFIG_P4V_PING_ON_BOOT
    ping_gw();
#endif
    // 消费者必须先于生产者启动；否则第一帧能入 latest 队列，但无人归还槽。
    if (xTaskCreate(display_task, "video_show", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create display task");
        set_note("display task FAILED");
        vTaskDelete(NULL);
        return;
    }
    pull_frames_forever();
}

void video_stream_start(lv_obj_t *canvas)
{
    s_canvas = canvas;
    s_wr = 0;
    set_note("starting");
    // 8KB 栈：socket + jpeg 驱动 + ESP_LOG 的格式化都在这个任务里跑。
    // p4_sta_host 那版只收不解码用 4KB 够，加上解码链路留一倍余量。
    xTaskCreate(video_task, "video", 8192, NULL, 5, NULL);
}
