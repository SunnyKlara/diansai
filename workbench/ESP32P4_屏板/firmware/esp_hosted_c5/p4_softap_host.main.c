/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* ---- LOCAL ADDITION: MJPEG sink over TCP (see the block at the bottom) ---- */
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#define VIDEO_PORT        5000
#define AP_IP_ADDR        "192.168.7.1"  /* deliberately NOT 192.168.4.x -- see wifi_init_softap */
#define MAX_FRAME_BYTES   (128 * 1024)   /* 640x480 JPEG is ~40-60 kB; 128 kB is slack */
static void video_sink_task(void *arg);
static void video_client_task(void *arg);
static void sta_watch_task(void *arg);

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define EXAMPLE_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define EXAMPLE_GTK_REKEY_INTERVAL 0
#endif

static const char *TAG = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    /* LOCAL: log every id. Under esp_hosted the ids arrive re-mapped (observed:
     * "rx RPC WifiEventNoArgs id=43" at the exact moment a station joined, while
     * this handler's WIFI_EVENT_AP_STACONNECTED branch never fired), so matching
     * on named constants alone silently hides the event we care about. */
    ESP_LOGI(TAG, "wifi event base=%s id=%ld", event_base ? event_base : "?", (long)event_id);
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    /* LOCAL: move the AP off 192.168.4.x.
     * The ESP-01S SoftAP on the car (DIANSAI_CAR) also serves 192.168.4.0/24 with
     * gateway 192.168.4.1, and the K230's network stack survives a MicroPython soft
     * reboot ("Network (rt-smart) is always active"). So after testing against the
     * car AP, the K230 kept a 192.168.4.3 lease and an ARP entry for 192.168.4.1
     * pointing at the ESP-01S -- indistinguishable from a fresh lease here, and a
     * perfect way to send SYNs into a black hole (observed: connect -> ENOTCONN
     * while association and DHCP both looked fine).
     * A distinct subnet makes the client's own IP the proof of which AP it joined. */
    {
        esp_netif_ip_info_t ip = { 0 };
        ip.ip.addr      = esp_ip4addr_aton(AP_IP_ADDR);
        ip.gw.addr      = esp_ip4addr_aton(AP_IP_ADDR);
        ip.netmask.addr = esp_ip4addr_aton("255.255.255.0");
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            /* LOCAL: upstream demands PMF (802.11w). Measured on this setup: the
             * K230 (RT-Thread wlan stack) associates with an ESP8266 AP in 2 s but
             * times out on this AP, while seeing it at -18 dBm with
             * security=SECURITY_WPA2_AES_PSK -- so it is a negotiation issue, not
             * range or cipher support. PMF is the prime suspect, and IDF leaves
             * .capable = true by default, so switching off "required" alone is not
             * enough: both flags have to go. Tighten again once the link is proven. */
            .pmf_cfg = {
                    .capable  = false,
                    .required = false,
            },
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
#endif
            .gtk_rekey_interval = EXAMPLE_GTK_REKEY_INTERVAL,
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* LOCAL: password no longer printed (captured logs get archived/pasted). */
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_CHANNEL);

    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (ap && esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
        ESP_LOGI(TAG, "AP addr " IPSTR " -- point the sender at " IPSTR ":%d",
                 IP2STR(&ip.ip), IP2STR(&ip.ip), VIDEO_PORT);
    }
}

/* ============ LOCAL ADDITION: MJPEG sink + throughput measurement ============
 * Why this exists: README §八 quotes 44 Mbps for SDIO 4-bit from Espressif's own
 * iperf table, but nothing has ever been measured on THIS board -- and the
 * co-processor is currently stuck in the slower "streaming" mode because its
 * factory firmware (2.12.9) predates SW_AGGR. A number measured here is the one
 * fact that decides whether plan D (MJPEG video over this link) is viable.
 *
 * Wire format (we own both ends, so keep it dumb and resyncable):
 *     'J' 'F' | uint32 length (little endian) | <length bytes of JPEG>
 * The magic lets the receiver notice desync instead of silently reading garbage.
 * ========================================================================== */
static void sink_stats(uint32_t frames, uint64_t bytes, uint32_t win_ms,
                       uint32_t fmin, uint32_t fmax)
{
    /* bits / ms = kbit/s  =>  Mbps*100 = bytes*8 / (ms*10).
     * (The first version was 100x too big; see the note in p4_sta_host/main/main.c.) */
    const uint32_t ms = win_ms ? win_ms : 1;
    uint32_t mbps_x100 = (uint32_t)((bytes * 8ULL) / (10ULL * ms));
    ESP_LOGI(TAG, "SINK %lu frames in %lu ms -> %lu.%02lu fps | %lu.%02lu Mbps | frame %lu..%lu B",
             (unsigned long)frames, (unsigned long)win_ms,
             (unsigned long)(frames * 1000UL / ms),
             (unsigned long)((frames * 100000UL / ms) % 100),
             (unsigned long)(mbps_x100 / 100), (unsigned long)(mbps_x100 % 100),
             (unsigned long)fmin, (unsigned long)fmax);
}

/* LOCAL: which side opens the TCP connection is a real experiment, not a taste.
 * K230 -> P4 (K230 connects) reproducibly dies with ENOTCONN on the K230 while
 * association and DHCP are fine. Flipping it (P4 connects out to a server on the
 * K230) tests whether the DATA PATH works at all on the current mismatched stack:
 *   - works  => plan D can proceed now, no co-processor OTA needed first
 *   - fails  => the path itself is broken, and the OTA really is the prerequisite
 * Both directions are compiled in; the client just retries in the background. */
#define PEER_PORT        5001
#define PEER_IP_FIRST    2      /* the AP's DHCP pool starts at .2, but don't assume */
#define PEER_IP_LAST     6

static bool read_exact(int sock, uint8_t *dst, size_t want)
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

/* LOCAL: the frame-reading loop, shared by both directions (server-accept and
 * client-connect) so the two experiments differ ONLY in how the socket was made. */
static void drain_frames(int cs, uint8_t *frame, const char *who)
{
    uint32_t frames = 0, fmin = 0xFFFFFFFF, fmax = 0, bad = 0;
    uint64_t bytes = 0;
    int64_t t_win = esp_timer_get_time();

    while (1) {
        uint8_t hdr[6];
        if (!read_exact(cs, hdr, sizeof(hdr))) {
            break;
        }
        if (hdr[0] != 'J' || hdr[1] != 'F') {
            if (++bad < 5) {
                ESP_LOGW(TAG, "%s bad magic %02x %02x -- stream desync", who, hdr[0], hdr[1]);
            }
            continue;
        }
        uint32_t len = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) |
                       ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);
        if (len == 0 || len > MAX_FRAME_BYTES) {
            ESP_LOGE(TAG, "%s frame length %lu out of range (max %d) -- dropping link",
                     who, (unsigned long)len, MAX_FRAME_BYTES);
            break;
        }
        if (!read_exact(cs, frame, len)) {
            break;
        }
        bool jpeg_ok = (frame[0] == 0xFF && frame[1] == 0xD8 &&
                        frame[len - 2] == 0xFF && frame[len - 1] == 0xD9);
        if (frames == 0) {
            ESP_LOGI(TAG, "%s first frame %lu B jpeg_markers=%s",
                     who, (unsigned long)len, jpeg_ok ? "OK" : "BAD");
        } else if (!jpeg_ok && bad < 5) {
            bad++;
            ESP_LOGW(TAG, "%s frame %lu B without FFD8..FFD9", who, (unsigned long)len);
        }

        frames++;
        bytes += len;
        if (len < fmin) { fmin = len; }
        if (len > fmax) { fmax = len; }

        int64_t now = esp_timer_get_time();
        if (now - t_win >= 1000000) {
            sink_stats(frames, bytes, (uint32_t)((now - t_win) / 1000), fmin, fmax);
            frames = 0; bytes = 0; fmin = 0xFFFFFFFF; fmax = 0;
            t_win = now;
        }
    }
    ESP_LOGW(TAG, "%s peer gone (bad_frames=%lu)", who, (unsigned long)bad);
}

/* LOCAL: direction B -- the P4 dials out to a server on the station. */
static void video_client_task(void *arg)
{
    uint8_t *frame = malloc(MAX_FRAME_BYTES);
    if (!frame) {
        ESP_LOGE(TAG, "CLIENT cannot allocate frame buffer");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        for (int host = PEER_IP_FIRST; host <= PEER_IP_LAST; host++) {
            char ip[16];
            snprintf(ip, sizeof(ip), "192.168.7.%d", host);
            int cs = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (cs < 0) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
            setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            struct sockaddr_in a = { 0 };
            a.sin_family = AF_INET;
            a.sin_port   = htons(PEER_PORT);
            a.sin_addr.s_addr = inet_addr(ip);
            if (connect(cs, (struct sockaddr *)&a, sizeof(a)) == 0) {
                int one = 1;
                setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                struct timeval rtv = { .tv_sec = 10, .tv_usec = 0 };
                setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
                ESP_LOGI(TAG, "CLIENT connected to %s:%d -- direction B works", ip, PEER_PORT);
                drain_frames(cs, frame, "CLIENT");
            }
            close(cs);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void video_sink_task(void *arg)
{
    uint8_t *frame = malloc(MAX_FRAME_BYTES);
    if (!frame) {
        ESP_LOGE(TAG, "SINK cannot allocate %d B frame buffer", MAX_FRAME_BYTES);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (ls < 0) {
            ESP_LOGE(TAG, "SINK socket() failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in addr = { 0 };
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(VIDEO_PORT);
        if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0) {
            ESP_LOGE(TAG, "SINK bind/listen failed errno=%d", errno);
            close(ls);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "SINK listening on tcp/%d (expecting 'JF'+u32len+jpeg)", VIDEO_PORT);

        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cs = accept(ls, (struct sockaddr *)&peer, &plen);
        if (cs < 0) {
            ESP_LOGE(TAG, "SINK accept failed errno=%d", errno);
            close(ls);
            continue;
        }
        setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ESP_LOGI(TAG, "SINK client %s connected", inet_ntoa(peer.sin_addr));
        drain_frames(cs, frame, "SINK");
        close(cs);
        close(ls);
    }
}
/* LOCAL: prints the AP's own view of who is associated, every 3 s, but only when
 * it changes -- so the log stays readable while still being the ground truth. */
static void sta_watch_task(void *arg)
{
    int last = -1;
    while (1) {
        wifi_sta_list_t list = { 0 };
        esp_err_t e = esp_wifi_ap_get_sta_list(&list);
        if (e != ESP_OK) {
            if (last != -2) {
                ESP_LOGW(TAG, "STAWATCH esp_wifi_ap_get_sta_list -> %s", esp_err_to_name(e));
                last = -2;
            }
        } else if (list.num != last) {
            last = list.num;
            ESP_LOGI(TAG, "STAWATCH %d station(s) associated", list.num);
            for (int i = 0; i < list.num; i++) {
                ESP_LOGI(TAG, "STAWATCH   [%d] " MACSTR " rssi=%d", i,
                         MAC2STR(list.sta[i].mac), list.sta[i].rssi);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
/* ============ end LOCAL ADDITION ========================================== */

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    /* LOCAL: start the MJPEG sink so the link can be measured end to end. */
    xTaskCreate(video_sink_task, "video_sink", 4096, NULL, 5, NULL);

    /* LOCAL: authoritative witness for "is anything actually associated to me".
     * Debugging the client side was going in circles because association, DHCP
     * and TCP all had to be inferred from the client's own (ambiguous) report --
     * both candidate APs served 192.168.4.0/24, so even its IP proved nothing.
     * The AP knows the truth: ask it. */
    xTaskCreate(sta_watch_task, "sta_watch", 3072, NULL, 4, NULL);

    /* LOCAL: direction-B experiment -- the P4 dials out to a server on the station,
     * so both connection directions are exercised from one firmware. */
    xTaskCreate(video_client_task, "video_client", 4096, NULL, 5, NULL);
}
