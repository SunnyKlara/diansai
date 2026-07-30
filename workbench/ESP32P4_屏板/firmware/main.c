// -*- coding: utf-8 -*-
// ESP32-P4 MIPI-DSI 屏幕 + LVGL widgets demo（面板描述表驱动）。
// 换屏只需改 lcd_panel_select.h 的 LCD_PANEL_ACTIVE；加新屏见 lcd_panel.h 说明。

#include "driver/gpio.h"
#include "esp_lcd_types.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ai_panel.h"
#include "imu_qmi8658.h"
#include "lcd_panel.h"
#include "lcd_panel_select.h"
#include "lvgl_demo.h"
#include "player.h"
#include "recorder.h"
#include "sdcard.h"
#include "video_stream.h"

static const char *TAG = "p4_lcd";

// 背光归属验证已挪进 ai_panel.c（那里能让屏上提示与 GPIO 动作同步，人眼才判得出；
// 放在这里闪 0.5s×3 实测"看不清、也不知道测试有没在跑"）。此处保留开关但默认关，
// 避免两处同时操作同一个 GPIO 打架。
#ifndef BL_BLINK_TEST
#define BL_BLINK_TEST  0
#endif

// ============================================================
// 🔌 屏幕 + 触摸全部 6 个 IO 统一在这里配置（依据 AIbox 新原理图）
//    各屏共用同一组引脚。
// ============================================================
#define PIN_LCD_RST   GPIO_NUM_26   // LCD 与触摸共用复位
#define PIN_LCD_BL    GPIO_NUM_30   // LCD 背光（高=亮）
#define PIN_TP_SDA    GPIO_NUM_28   // 触摸 I2C SDA
#define PIN_TP_SCL    GPIO_NUM_29   // 触摸 I2C SCL
#define PIN_TP_INT    GPIO_NUM_27   // 触摸中断
#define PIN_TP_RST    PIN_LCD_RST   // 触摸复位与 LCD 共用 GPIO26

// 板载 QMI8658A 六轴 IMU 挂在 I2C0（与 ES8389 音频 codec 共用这条总线，地址不冲突）
#define PIN_IMU_SDA   7
#define PIN_IMU_SCL   8

// microSD 顺序读写测速的数据量（KB）。太小则被缓存/首次寻道淹没，2MB 够看出量级
// 0 = 跳过测速。数据已经拿到并复现两次（8 MB 稳态：写 424 / 读 1579 KB/s，见 README §10.14），
// 而它要占 25 s 开机时间、把录像自测推到 30 s 之后，和 K230 那侧 60 s 的推流窗口对不上。
// 需要重测时改回 8192（2048 太小：首次簇分配和卡写缓存会把持续写速度盖住）。
#define SD_BENCH_KB   0

// 开机自动录一段的秒数（0=关）。录像链路已真机验过（278 帧 / 丢 0 / 文件大小逐字节吻合，
// README §10.15），且触摸按钮已就绪 ⇒ 关掉它，把开机时间让给回放自测。
#ifndef P4_REC_AUTOTEST_SEC
#define P4_REC_AUTOTEST_SEC 0
#endif

// 开机自动回放一段的秒数（0=关）。为什么需要：回放只能靠点屏触发，而 AI 点不了屏，
// 于是用它把「原速播放 / 暂停 / 拖动」三件事跑出**定量判据**（见 play_autotest_task）。
// 触摸按钮由人验；这个自测验的是按钮背后的 player 链路。
#ifndef P4_PLAY_AUTOTEST_SEC
#define P4_PLAY_AUTOTEST_SEC 12
#endif

#if P4_ENABLE_SDCARD && P4_REC_AUTOTEST_SEC > 0
// 内嵌的那张 K230 真实产出的 JPEG。K230 不在线时用它按图传帧率灌进录像模块，
// 把"网络"这个变量摘掉、单独验证写盘链路（帧数/文件/时间戳/丢帧率）。
// ⚠️ 这样录出来的不是真实画面，只能证明**录像模块**对，不能证明端到端功能。
extern const uint8_t k230_frame1_start[] asm("_binary_k230_frame1_jpg_start");
extern const uint8_t k230_frame1_end[]   asm("_binary_k230_frame1_jpg_end");

static void rec_autotest_task(void *arg)
{
    (void)arg;
    // 等 100 s。为什么这么长：C5 会**间歇性不发 esp-hosted init event**（实测同一固件
    // 连续两次复位，一次 2646ms 就报 `eh_init_evt: slave chip id 0x17`、wifi 正常，
    // 另一次 2615ms 之后一片空白、`esp_wifi_init` 直接 ESP_FAIL），需要人为重试复位；
    // 加上 K230 那侧起相机也要十几秒。窗口短了就总落到内嵌帧兜底上，看不到真实录像。
    video_stats_t vs;
    for (int i = 0; i < 200; ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
        video_stream_get_stats(&vs);
        if (vs.frames > 10) {
            break;
        }
    }
    video_stream_get_stats(&vs);
    const bool have_live = (vs.frames > 10);
    if (have_live) {
        ESP_LOGW(TAG, "🎬 录像自测：用**实时图传帧**（已收 %lu 帧），录 %d s",
                 (unsigned long)vs.frames, P4_REC_AUTOTEST_SEC);
    } else {
        ESP_LOGW(TAG, "🎬 录像自测：30 s 无图传帧 -> 改用**内嵌帧**灌录 %d s，"
                      "只验写盘链路，画面不是实拍", P4_REC_AUTOTEST_SEC);
    }
    if (recorder_start() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    const uint32_t emb_len = (uint32_t)(k230_frame1_end - k230_frame1_start);
    const int64_t  t_end = esp_timer_get_time() + (int64_t)P4_REC_AUTOTEST_SEC * 1000000;
    int64_t t_rep = esp_timer_get_time();
    while (esp_timer_get_time() < t_end && recorder_is_recording()) {
        if (have_live) {
            vTaskDelay(pdMS_TO_TICKS(200));       // 实时帧由 RX 任务自己 feed
        } else {
            // 18 ms 一帧 ≈ 55 fps，和实测图传帧率同量级 ⇒ 抽帧 1/4 后约 14 fps，
            // 这样测出来的写盘压力才和真实录像可比。
            recorder_feed(k230_frame1_start, emb_len);
            vTaskDelay(pdMS_TO_TICKS(18));
        }
        if (esp_timer_get_time() - t_rep >= 5000000) {
            t_rep = esp_timer_get_time();
            recorder_stats_t rs;
            recorder_get_stats(&rs);
            ESP_LOGW(TAG, "🎬 录像中 %lu 帧 / %lu KB / %lu ms（跳 %lu 丢 %lu）",
                     (unsigned long)rs.frames, (unsigned long)rs.kbytes,
                     (unsigned long)rs.ms, (unsigned long)rs.skipped,
                     (unsigned long)rs.dropped);
        }
    }
    recorder_stop();
    // 停录后列一次目录：文件真的落地了没、大小对不对（这是"录成了"的最终判据）
    sdcard_list_root();
    vTaskDelete(NULL);
}
#endif

// DMA 内存水位探针。为什么需要：实测 C5 的 esp-hosted 在 2642ms 报
//   `eh_sdio: dma_alloc(278528) failed; dropping read` ⇒ **C5 的 init event 是被 P4 丢掉的**，
// 不是 C5 不发。而 272 KB 这种大块分配可能死在"最大连续块"上而非总量，所以两个都要打。
static void dma_watermark(const char *stage)
{
    ESP_LOGW(TAG, "DMAWM %-14s free=%u largest=%u (internal: free=%u largest=%u)",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

#if P4_ENABLE_SDCARD && P4_PLAY_AUTOTEST_SEC > 0
// 回放自测：列片 → 放最后一片 → 暂停（验"真的停住"）→ 拖到 0 帧 → 继续（验"真的在推进"）。
// 每一步都打出帧号，靠帧号变化而不是"看起来在动"来判定。
static void play_autotest_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));            // 等 LVGL/面板稳住再动
    player_clip_t clips[PLAYER_MAX_CLIPS];
    const int n = player_list(clips, PLAYER_MAX_CLIPS);
    ESP_LOGW(TAG, "PLAYTEST 卡上 %d 片", n);
    for (int i = 0; i < n && i < 4; ++i) {
        ESP_LOGW(TAG, "PLAYTEST   clip #%lu %s %lu B",
                 (unsigned long)clips[i].index, clips[i].name,
                 (unsigned long)clips[i].bytes);
    }
    if (n == 0 || player_open(0) != ESP_OK) {
        ESP_LOGW(TAG, "PLAYTEST 跳过（没有可放的片或打开失败）");
        vTaskDelete(NULL);
        return;
    }
    player_status_t st;
    for (int s = 0; s < P4_PLAY_AUTOTEST_SEC; ++s) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        player_get_status(&st);
        if (st.state == PLAYER_IDLE) {
            ESP_LOGW(TAG, "PLAYTEST 已播完，停在第 %lu 帧", (unsigned long)st.frame);
            break;
        }
        if ((s % 4) == 3) {
            ESP_LOGW(TAG, "PLAYTEST 放中 %s %lu/%lu 帧 ts=%lu ms inject_retry=%lu",
                     st.name, (unsigned long)st.frame, (unsigned long)st.total_frames,
                     (unsigned long)st.ts_ms, (unsigned long)st.inject_fail);
        }
    }
    // —— 暂停判据：停 3 s 后帧号必须不变 ——
    player_pause();
    vTaskDelay(pdMS_TO_TICKS(200));
    player_status_t a;
    player_get_status(&a);
    vTaskDelay(pdMS_TO_TICKS(3000));
    player_status_t b;
    player_get_status(&b);
    ESP_LOGW(TAG, "PLAYTEST 暂停: 3s 内帧号 %lu -> %lu  => %s",
             (unsigned long)a.frame, (unsigned long)b.frame,
             (a.frame == b.frame) ? "PASS(真停住)" : "FAIL(还在走)");
    // —— 拖动判据：seek(0) 后帧号必须归 0 ——
    (void)player_seek(0);
    vTaskDelay(pdMS_TO_TICKS(300));
    player_status_t c;
    player_get_status(&c);
    ESP_LOGW(TAG, "PLAYTEST 拖动: seek(0) 后帧号=%lu => %s",
             (unsigned long)c.frame, (c.frame == 0) ? "PASS" : "FAIL");
    // —— 继续判据：3 s 后帧号必须推进 ——
    player_resume();
    vTaskDelay(pdMS_TO_TICKS(3000));
    player_status_t d;
    player_get_status(&d);
    ESP_LOGW(TAG, "PLAYTEST 继续: 3s 后帧号=%lu => %s（约 14fps 预期 40 上下）",
             (unsigned long)d.frame, (d.frame > 0) ? "PASS" : "FAIL");
    player_stop();
    ESP_LOGW(TAG, "PLAYTEST 结束，已回到实时画面");
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    dma_watermark("app_main");
    const lcd_panel_desc_t *pd = &LCD_PANEL_ACTIVE;
    ESP_LOGI(TAG, "🚀 ESP32-P4 %s MIPI LCD + LVGL 启动", pd->name);

    const lcd_touch_pins_t tp_pins = {
        .sda = PIN_TP_SDA,
        .scl = PIN_TP_SCL,
        .irq = PIN_TP_INT,
        .rst = PIN_TP_RST,
    };

    // 1. 面板上电 + 初始化；GPIO26 会同时复位 LCD 与触摸控制器
    esp_lcd_panel_handle_t panel = NULL, control_panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    if (pd->lcd_init(PIN_LCD_RST, PIN_LCD_BL, &tp_pins,
                     &panel, &control_panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "❌ 屏幕初始化失败");
        return;
    }

    // 2. 面板流程未提前创建触摸时，在 LVGL 启动前执行兜底创建
    if (pd->touch_prepare) {
        const esp_err_t touch_ret = pd->touch_prepare(&tp_pins, pd->h_res, pd->v_res);
        if (touch_ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ LCD 初始化后触摸创建失败：%s，继续启动显示",
                     esp_err_to_name(touch_ret));
        }
    }

    // 3. 板载外设自检（在 LVGL 之前，好让面板创建时就能显示状态）
    //    两者都是"失败不影响显示"，故只记日志、不 return
    if (imu_qmi8658_init(PIN_IMU_SDA, PIN_IMU_SCL) != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ QMI8658A 初始化失败，其余功能继续");
    }
#if P4_ENABLE_SDCARD
    // 2026-07-30：改回 SDMMC 4-line。原来那句"必须用 SPI，SDMMC 与 C5 共用控制器必然失败"
    // 是错的（README §10.13 ⑧）—— 真正缺的是 P4 的 SDMMC IO 供电（片上 LDO_VO4），
    // 上游 sila-p4c5/sd_scanner.c 用的也是 SDMMC。SPI 那条路只是错因果下的弯路。
    if (sdcard_mount() != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ microSD 未挂载，其余功能继续（录像/回放将不可用）");
    }
#else
    ESP_LOGW(TAG, "ℹ️ microSD 本次编译已关闭（见 sdcard.h）");
#endif

    dma_watermark("after_sd");
    // 4. 启动 LVGL，并把已创建的触摸设备挂到输入设备
    if (lvgl_start(pd, panel, control_panel, io, &tp_pins) != ESP_OK) {
        ESP_LOGE(TAG, "❌ LVGL 启动失败");
        return;
    }

    dma_watermark("after_lvgl");
    ESP_LOGI(TAG, "✅ %s + LVGL 界面已启动（%lux%lu）",
             pd->name, (unsigned long)pd->h_res, (unsigned long)pd->v_res);

    // 5. ⚠️ 这里**故意不读 IMU**。
    //    第一版在此处做了一次"自检读取"，而 ai_panel 的 LVGL 定时器同时也在周期读，
    //    两个任务并发操作同一个 i2c_master 设备句柄 => I2C 状态机被打乱，实测串口
    //    被 "i2c.master: clear bus failed / reset hardware failed" 刷屏、之后再也读不出数。
    //    改为**单一调用点**：只有 LVGL 任务读 I2C，定量判据也由 ai_panel 定期打印到串口。

    // 6. microSD 顺序读写测速（挂载成功才跑；会在卡上建 _bench.bin 并删除）
    if (sdcard_info()->mounted) {
        sdcard_dump_mbr();        // 59GB 在哪个分区（只读）
        sdcard_list_root();       // 先看清卡上有什么（只读），再谈能不能动它
        if (SD_BENCH_KB > 0) {
            sdcard_bench_t b;
            sdcard_benchmark(SD_BENCH_KB, &b);
        }
    }

    // 7. 方案 D 无线图传：K230(AP+编码+server) --WiFi--> C5 --SDIO--> P4(解码) --> 屏
    //    放在最后启动的两个理由：
    //      ① LVGL 已经在跑，所以「连接中/收帧中」的状态能实时打在屏上；
    //      ② esp_wifi_init 会跨 SDIO 把 C5 拉起来，耗时以秒计，不该挡住点屏。
    //    canvas 由 ai_panel 创建，收帧任务在 lvgl_port 锁内换缓冲。
    lv_obj_t *video_canvas = ai_panel_video_canvas();
    if (video_canvas == NULL) {
        ESP_LOGW(TAG, "⚠️ 没有 video canvas（当前 demo 不是 ai_panel？），无线图传只收帧不上屏");
    }
    video_stream_start(video_canvas);

#if P4_ENABLE_SDCARD
    // 8. 录像模块（旁路：抽帧 + 拷贝 + 独立写盘任务，不反压网络接收）
    if (sdcard_info()->mounted && player_init() != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ 回放模块初始化失败，PLAY/暂停/拖动按钮将无响应");
    }
#if P4_PLAY_AUTOTEST_SEC > 0
    if (sdcard_info()->mounted) {
        xTaskCreate(play_autotest_task, "play_test", 4096, NULL, 3, NULL);
    }
#endif
    if (sdcard_info()->mounted && recorder_init() == ESP_OK) {
#if P4_REC_AUTOTEST_SEC > 0
        // 触摸按钮还没做，先用"开机自动录一段"把录像功能的真机数据拿到：
        // 写盘跟不跟得上（看 dropped）、文件生成对不对、时长准不准。
        xTaskCreate(rec_autotest_task, "rec_test", 3072, NULL, 3, NULL);
#endif
    }
#endif

    dma_watermark("after_video");
#if BL_BLINK_TEST
    // 等界面画出来再闪，否则黑屏闪看不出所以然
    vTaskDelay(pdMS_TO_TICKS(2500));
    for (int i = 1; i <= 3; i++) {
        gpio_set_level(PIN_LCD_BL, 0);
        ESP_LOGI(TAG, "🔅 背光 OFF #%d (GPIO%d=0) —— 屏若变黑则 IO30 真管背光", i, PIN_LCD_BL);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(PIN_LCD_BL, 1);
        ESP_LOGI(TAG, "🔆 背光 ON  #%d (GPIO%d=1)", i, PIN_LCD_BL);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
    ESP_LOGI(TAG, "🔎 背光通断测试结束：屏幕全程未变暗 => 背光与 IO30 无关（硬件常亮）");
#endif
}
