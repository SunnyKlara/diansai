// -*- coding: utf-8 -*-
// 见 video_stream.h：无线收帧 + 硬件解码 + 上屏，一个任务串起来。
// 注释里的数字凡标 MEASURED 的都是本板真机实测，其余标 待验证。

#include "video_stream.h"

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

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
// （画面剧烈变化时）。96KB 一次性预留，收帧直接 recv 进这块 DMA 缓冲，省一次拷贝。
#define FRAME_MAX_BYTES   (96 * 1024)

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

// 解码资源：建一次复用。jpeg_view.c 那版是一次性的（建引擎→解一帧→删引擎），
// 每帧重建引擎在 50fps 下纯属浪费。
static jpeg_decoder_handle_t s_dec;
static uint8_t *s_in;                 // DMA 输入：直接 recv 进来
static size_t   s_in_alloc;
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

    s_in = jpeg_alloc_decoder_mem(FRAME_MAX_BYTES, &in_cfg, &s_in_alloc);
    for (int i = 0; i < 2; i++) {
        s_out[i] = jpeg_alloc_decoder_mem(DEC_OUT_BYTES, &out_cfg, &s_out_alloc);
    }
    if (!s_in || !s_out[0] || !s_out[1]) {
        ESP_LOGE(TAG, "jpeg buffers: in=%u out=%u (need %u + 2x%u)",
                 (unsigned)s_in_alloc, (unsigned)s_out_alloc,
                 (unsigned)FRAME_MAX_BYTES, (unsigned)DEC_OUT_BYTES);
        set_note("jpeg buffer alloc FAILED");
        return false;
    }
    ESP_LOGI(TAG, "decoder ready: in %u B, out 2 x %u B", (unsigned)s_in_alloc, (unsigned)s_out_alloc);
    return true;
}

// 解一帧到 s_out[idx]，成功则把该缓冲挂上 canvas。
static bool decode_and_show(uint32_t len)
{
    jpeg_decode_picture_info_t info = { 0 };
    esp_err_t err = jpeg_decoder_get_info(s_in, len, &info);
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
    err = jpeg_decoder_process(s_dec, &cfg, s_in, len, s_out[idx],
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
            s_wr ^= 1;                    // 换手：下一帧写另一块
        } else {
            // 拿不到锁就别换手，宁可这一帧不上屏（也别去动 LVGL 正在读的缓冲）
            ESP_LOGW(TAG, "lvgl lock timeout, frame not displayed");
        }
    } else {
        s_wr ^= 1;
    }
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
        ESP_LOGI(TAG, "connected -- pulling frames");
        s_st.stream_up = true;
        set_note("streaming");

        uint32_t win_frames = 0;
        uint64_t win_bytes = 0;
        int64_t  t_win = esp_timer_get_time();
        uint32_t link_frames = 0;

        while (1) {
            uint8_t hdr[6];
            if (!rx_exact(cs, hdr, sizeof(hdr))) {
                break;
            }
            if (hdr[0] != 'J' || hdr[1] != 'F') {
                if (++s_st.bad < 5) {
                    ESP_LOGW(TAG, "bad magic %02x %02x", hdr[0], hdr[1]);
                }
                continue;
            }
            const uint32_t len = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) |
                                 ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);
            if (len == 0 || len > s_in_alloc) {
                ESP_LOGE(TAG, "frame length %lu exceeds buffer %u -- raise FRAME_MAX_BYTES",
                         (unsigned long)len, (unsigned)s_in_alloc);
                s_st.bad++;
                break;                    // 长度不可信 => 流已错位，重连比硬啃安全
            }
            if (!rx_exact(cs, s_in, len)) {
                break;
            }
            const bool markers_ok = (s_in[0] == 0xFF && s_in[1] == 0xD8 &&
                                     s_in[len - 2] == 0xFF && s_in[len - 1] == 0xD9);
            if (!markers_ok) {
                s_st.bad++;
                continue;
            }
            if (link_frames == 0) {
                ESP_LOGI(TAG, "first frame %lu B, markers OK", (unsigned long)len);
            }
            s_st.last_len = len;
            decode_and_show(len);
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
                ESP_LOGI(TAG, "%lu.%lu fps | %lu.%02lu Mbps | frame %lu B | decode %lu us | total %lu bad %lu",
                         (unsigned long)(s_st.fps_x10 / 10), (unsigned long)(s_st.fps_x10 % 10),
                         (unsigned long)(s_st.mbps_x100 / 100), (unsigned long)(s_st.mbps_x100 % 100),
                         (unsigned long)s_st.last_len, (unsigned long)s_st.dec_us,
                         (unsigned long)s_st.frames, (unsigned long)s_st.bad);
                win_frames = 0;
                win_bytes = 0;
                t_win = now;
            }
        }
        ESP_LOGW(TAG, "link closed after %lu frames (total %lu, bad %lu)",
                 (unsigned long)link_frames, (unsigned long)s_st.frames, (unsigned long)s_st.bad);
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
