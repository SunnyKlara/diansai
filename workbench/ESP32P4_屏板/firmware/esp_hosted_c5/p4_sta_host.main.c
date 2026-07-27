/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* ---------------- LOCAL ADDITION (not upstream) ------------------------------
 * The upstream example stops at "got ip", which already proves bidirectional
 * traffic (DHCP needs a round trip), but the acceptance criterion for this board
 * is literally "P4 can ping". So after the lease we ICMP-ping the gateway and
 * print a machine-greppable verdict line. Also gives a first RTT number for the
 * P4 -> SDIO -> C5 -> air path, which the video-streaming plan needs anyway.
 * -------------------------------------------------------------------------- */
#include <inttypes.h>
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

/* ---- LOCAL ADDITION: gateway learnt from DHCP, then pinged ---------------- */
/* Fallback used when the AP associates but never hands out a lease. Matches the
 * ESP-01S SoftAP on the car (gateway 192.168.4.1, .2 is taken by the PC-side
 * module, so we take .3). Set STATIC_FALLBACK_ENABLE to 0 to test DHCP only. */
#define STATIC_FALLBACK_ENABLE   1
#define STATIC_FALLBACK_IP       "192.168.4.3"
#define STATIC_FALLBACK_GW       "192.168.4.1"
#define STATIC_FALLBACK_MASK     "255.255.255.0"
#define DHCP_WAIT_MS             12000

static esp_netif_t *s_sta_netif;
static esp_ip4_addr_t s_gw_ip;
#define WIFI_ASSOCIATED_BIT BIT2
static EventGroupHandle_t s_ping_done_group;
#define PING_DONE_BIT BIT0

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_ms, recv_len;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
    ESP_LOGI(TAG, "PING reply seq=%u from %s bytes=%" PRIu32 " ttl=%u time=%" PRIu32 " ms",
             (unsigned)seqno, inet_ntoa(target_addr.u_addr.ip4),
             recv_len, (unsigned)ttl, elapsed_ms);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    ESP_LOGW(TAG, "PING timeout seq=%u", (unsigned)seqno);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t tx = 0, rx = 0, total_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_ms, sizeof(total_ms));
    /* single grep-able verdict line: the acceptance criterion for step 1 */
    ESP_LOGI(TAG, "PING SUMMARY tx=%" PRIu32 " rx=%" PRIu32 " loss=%" PRIu32 "%% duration=%" PRIu32 " ms -> %s",
             tx, rx, (tx ? (tx - rx) * 100 / tx : 100), total_ms,
             (rx > 0) ? "PING_OK" : "PING_FAIL");
    xEventGroupSetBits(s_ping_done_group, PING_DONE_BIT);
}

static void ping_gateway(void)
{
    if (s_gw_ip.addr == 0) {
        ESP_LOGE(TAG, "PING SUMMARY skipped -> PING_FAIL (no gateway from DHCP)");
        return;
    }
    s_ping_done_group = xEventGroupCreate();

    ip_addr_t target = { 0 };
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = s_gw_ip.addr;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = 5;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = on_ping_end,
        .cb_args         = NULL,
    };
    esp_ping_handle_t ping = NULL;
    if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK) {
        ESP_LOGE(TAG, "PING SUMMARY skipped -> PING_FAIL (session create failed)");
        return;
    }
    ESP_LOGI(TAG, "PING start -> " IPSTR " (5 packets)", IP2STR(&s_gw_ip));
    esp_ping_start(ping);
    xEventGroupWaitBits(s_ping_done_group, PING_DONE_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(20000));
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
}
/* ---- end LOCAL ADDITION -------------------------------------------------- */


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        /* LOCAL: separate "associated" from "has an IP" so a DHCP failure is
         * distinguishable from an association failure. */
        ESP_LOGI(TAG, "STA_CONNECTED (associated, waiting for DHCP)");
        xEventGroupSetBits(s_wifi_event_group, WIFI_ASSOCIATED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR " gw:" IPSTR " mask:" IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw),
                 IP2STR(&event->ip_info.netmask));   /* LOCAL: gw/mask added */
        s_gw_ip = event->ip_info.gw;                 /* LOCAL: ping target */
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();   /* LOCAL: keep handle */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    /* LOCAL: upstream waits forever; a bounded wait lets us report *why* it
     * failed (never associated vs associated-but-no-lease) instead of hanging
     * silently, which is exactly what happened on the first run of this board. */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(DHCP_WAIT_MS));

    /* LOCAL: is the radio's MAC actually readable? A broken esp_wifi_get_mac RPC
     * leaves the netif without a usable MAC, which silently kills DHCP -- prime
     * suspect while host and co-processor run mismatched esp_hosted majors. */
    uint8_t mac[6] = {0};
    esp_err_t mac_err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "sta mac %02x:%02x:%02x:%02x:%02x:%02x (esp_wifi_get_mac=%s)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], esp_err_to_name(mac_err));
    uint8_t nmac[6] = {0};
    esp_err_t nmac_err = esp_netif_get_mac(s_sta_netif, nmac);
    ESP_LOGI(TAG, "netif mac %02x:%02x:%02x:%02x:%02x:%02x (esp_netif_get_mac=%s)",
             nmac[0], nmac[1], nmac[2], nmac[3], nmac[4], nmac[5], esp_err_to_name(nmac_err));

#if STATIC_FALLBACK_ENABLE
    if (!(bits & WIFI_CONNECTED_BIT) && (xEventGroupGetBits(s_wifi_event_group) & WIFI_ASSOCIATED_BIT)) {
        ESP_LOGW(TAG, "no DHCP lease after %d ms -> falling back to static %s",
                 DHCP_WAIT_MS, STATIC_FALLBACK_IP);
        esp_netif_dhcpc_stop(s_sta_netif);
        esp_netif_ip_info_t ip = { 0 };
        ip.ip.addr      = esp_ip4addr_aton(STATIC_FALLBACK_IP);
        ip.gw.addr      = esp_ip4addr_aton(STATIC_FALLBACK_GW);
        ip.netmask.addr = esp_ip4addr_aton(STATIC_FALLBACK_MASK);
        if (esp_netif_set_ip_info(s_sta_netif, &ip) == ESP_OK) {
            s_gw_ip = ip.gw;
            bits |= WIFI_CONNECTED_BIT;   /* good enough to attempt the ping */
        } else {
            ESP_LOGE(TAG, "static IP set failed");
        }
    }
#endif

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    /* LOCAL: upstream logs the password here; dropped so captured serial logs
     * (which get pasted around and archived) never carry the PSK. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
        ping_gateway();                              /* LOCAL ADDITION */
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
        ESP_LOGE(TAG, "PING SUMMARY skipped -> PING_FAIL (never associated)");
    } else {
        /* LOCAL: bounded wait expired */
        ESP_LOGE(TAG, "PING SUMMARY skipped -> PING_FAIL (no IP and no static fallback)");
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
