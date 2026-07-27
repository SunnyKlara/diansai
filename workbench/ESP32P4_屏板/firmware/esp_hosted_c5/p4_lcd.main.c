// -*- coding: utf-8 -*-
// ESP32-P4 MIPI-DSI 屏幕 + LVGL widgets demo（面板描述表驱动）。
// 换屏只需改 lcd_panel_select.h 的 LCD_PANEL_ACTIVE；加新屏见 lcd_panel.h 说明。

#include "driver/gpio.h"
#include "esp_lcd_types.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ai_panel.h"
#include "imu_qmi8658.h"
#include "lcd_panel.h"
#include "lcd_panel_select.h"
#include "lvgl_demo.h"
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
#define SD_BENCH_KB   2048

void app_main(void)
{
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
    if (sdcard_mount() != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ microSD 未挂载（最常见=没插卡），其余功能继续");
    }
#else
    ESP_LOGW(TAG, "ℹ️ microSD 本次编译已关闭（见 sdcard.h：slot0 卡 vs slot1 C5 SDIO 共存未验证）");
#endif

    // 4. 启动 LVGL，并把已创建的触摸设备挂到输入设备
    if (lvgl_start(pd, panel, control_panel, io, &tp_pins) != ESP_OK) {
        ESP_LOGE(TAG, "❌ LVGL 启动失败");
        return;
    }

    ESP_LOGI(TAG, "✅ %s + LVGL 界面已启动（%lux%lu）",
             pd->name, (unsigned long)pd->h_res, (unsigned long)pd->v_res);

    // 5. ⚠️ 这里**故意不读 IMU**。
    //    第一版在此处做了一次"自检读取"，而 ai_panel 的 LVGL 定时器同时也在周期读，
    //    两个任务并发操作同一个 i2c_master 设备句柄 => I2C 状态机被打乱，实测串口
    //    被 "i2c.master: clear bus failed / reset hardware failed" 刷屏、之后再也读不出数。
    //    改为**单一调用点**：只有 LVGL 任务读 I2C，定量判据也由 ai_panel 定期打印到串口。

    // 6. microSD 顺序读写测速（挂载成功才跑；会在卡上建 _bench.bin 并删除）
    if (sdcard_info()->mounted) {
        sdcard_bench_t b;
        sdcard_benchmark(SD_BENCH_KB, &b);
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
