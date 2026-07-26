// -*- coding: utf-8 -*-
// 自绘验证面板（ESP32-P4 + 6.2寸 AXS15260 长条屏，逻辑分辨率 1280x452）。
//
// 目的不是好看，是「可判读」：
//   1. 屏上出现本文件画的内容 = 显示链路受固件控制（而不是厂家 demo 还在跑）；
//   2. 编译时间戳 = 板上跑的确实是刚烧进去的固件（防"以为烧进去了"）；
//   3. 运行秒数 + 心跳条 = LVGL 任务没卡死；
//   4. 跟手圆点 + 坐标读数 = 触摸坐标映射对不对（偏移/上下颠倒/左右镜像一眼可见）。
//
// UI 文本全部 ASCII：默认只启用了 montserrat 字体，没有中文字库。
#include "ai_panel.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"

#include "imu_qmi8658.h"
#include "sdcard.h"

static const char *TAG = "ai_panel";

// ============================================================
// 背光归属判决测试
// ------------------------------------------------------------
// 背景：出厂固件驱动的是 GPIO47，而原理图标 LCD_BL=IO30，且 P4 模组 IO39~IO54
// 多数未引出。到底谁管背光、还是硬件常亮，只有"主动拉低看屏灭不灭"能定论。
//
// 第一版把它放在 main.c 里闪 0.5s×3，结果人眼判不出（太快、且屏若没灭也不知道
// 测试有没有在跑）。这一版改成**自解释**序列，并挪进 ai_panel 以便与 UI 同步：
//   ① 拉低之前先在屏上写一行"如果你还能读到这行字，IO30 就管不着背光"；
//   ② 留 PRE_OFF 时间让 LVGL 把这一帧真正刷上屏（渲染是异步的，
//      同一 tick 内先 set_text 再拉 GPIO 会来不及显示）；
//   ③ 然后才拉低，并保持 OFF 足够长，人眼来得及看清。
//
// ✅ 结论（2026-07-27 真机人眼确认）：**GPIO30 就是背光控制脚**——按设定节奏黑屏了。
//    ⇒ 原理图 `LCD_BL = IO30` 正确；出厂固件里驱动 `GPIO47` 的那段是厂家 fork 遗留代码。
//    结论已定，故测试默认关闭（否则每 33s 闪一次背光会干扰正常使用）。
//    需要在别的板子/别的屏上重跑这个判决实验时，把下面开关改回 1。
// ============================================================
#ifndef AI_PANEL_BL_TEST
#define AI_PANEL_BL_TEST 0
#endif

#define BL_GPIO          30      // 必须与 main.c 的 PIN_LCD_BL 一致（原理图 LCD_BL）
#define BL_LEAD_MS       5000    // 开测前的准备时间（给人抬头看屏）
#define BL_PRE_OFF_MS    800     // 提示文字先上屏，背光仍亮
#define BL_OFF_MS        3000    // 背光拉低保持时长
#define BL_ON_MS         1500    // 恢复常亮时长
#define BL_ROUNDS        3
#define BL_IDLE_MS       12000   // 两轮测试之间的间歇（测试自动循环，见 bl_test_cb）

#define BL_ROUND_MS      (BL_PRE_OFF_MS + BL_OFF_MS + BL_ON_MS)
#define BL_TOTAL_MS      (BL_LEAD_MS + BL_ROUNDS * BL_ROUND_MS)
#define BL_TICK_MS       200

static lv_obj_t *s_lbl_uptime;
static lv_obj_t *s_lbl_touch;
static lv_obj_t *s_bar_beat;
static lv_obj_t *s_dot;
static lv_obj_t *s_lbl_imu;
static lv_obj_t *s_lbl_sd;
#if AI_PANEL_BL_TEST
static lv_obj_t *s_lbl_bl;

static uint32_t s_bl_ms;        // 背光测试已流逝毫秒
static int      s_bl_level = 1; // 当前 GPIO 电平，仅在变化时才写，避免每 tick 刷 GPIO
#endif

static uint32_t s_press_count;
static int32_t s_last_x = -1;
static int32_t s_last_y = -1;

#if AI_PANEL_BL_TEST
static void bl_set(int level, const char *why)
{
    if (s_bl_level == level) {
        return;
    }
    s_bl_level = level;
    gpio_set_level(BL_GPIO, level);
    ESP_LOGI(TAG, "backlight GPIO%d=%d  (%s)", BL_GPIO, level, why);
}

// 背光判决序列：由 lv_timer 驱动，保证屏上提示与 GPIO 动作同步
static void bl_test_cb(lv_timer_t *t)
{
    s_bl_ms += BL_TICK_MS;

    if (s_bl_ms < BL_LEAD_MS) {
        lv_label_set_text_fmt(s_lbl_bl,
                              "BACKLIGHT TEST (GPIO%d) starts in %lu s",
                              BL_GPIO,
                              (unsigned long)((BL_LEAD_MS - s_bl_ms) / 1000 + 1));
        lv_obj_set_style_text_color(s_lbl_bl, lv_color_hex(0x8899aa), 0);
        return;
    }

    const uint32_t in_test = s_bl_ms - BL_LEAD_MS;
    const uint32_t test_len = (uint32_t)BL_ROUNDS * BL_ROUND_MS;

    // 测试跑完不停手：歇 BL_IDLE_MS 再自动重跑。人不必和 AI 对时间，
    // 随时抬头看屏都能在一个周期内等到一次完整判决。
    if (in_test >= test_len) {
        bl_set(1, "idle between test cycles");
        const uint32_t left = (in_test >= test_len + BL_IDLE_MS)
                              ? 0 : (test_len + BL_IDLE_MS - in_test);
        lv_label_set_text_fmt(s_lbl_bl,
                              "BL TEST DONE - did the screen go FULLY DARK 3 times?\n"
                              "YES = GPIO%d drives backlight | NO = always-on hardware   (repeat in %lu s)",
                              BL_GPIO, (unsigned long)(left / 1000 + 1));
        lv_obj_set_style_text_color(s_lbl_bl, lv_color_hex(0x00ffc8), 0);
        if (left == 0) {
            s_bl_ms = 0;   // 回到 lead-in，重跑一轮
            ESP_LOGI(TAG, "backlight test cycle restarting");
        }
        return;
    }

    const uint32_t round = in_test / BL_ROUND_MS;         // 0..BL_ROUNDS-1
    const uint32_t phase = in_test % BL_ROUND_MS;

    if (phase < BL_PRE_OFF_MS) {
        // 关之前先把判据文字刷上屏（背光此刻仍亮）
        bl_set(1, "pre-off, showing the verdict text");
        lv_label_set_text_fmt(s_lbl_bl,
                              "ROUND %lu/%d  -  cutting GPIO%d NOW\n"
                              "IF YOU CAN STILL READ THIS LINE, GPIO%d IS *NOT* THE BACKLIGHT",
                              (unsigned long)(round + 1), BL_ROUNDS, BL_GPIO, BL_GPIO);
        lv_obj_set_style_text_color(s_lbl_bl, lv_color_hex(0xff5c7a), 0);
    } else if (phase < BL_PRE_OFF_MS + BL_OFF_MS) {
        bl_set(0, "OFF window - screen should be dark");
    } else {
        bl_set(1, "ON window");
        lv_label_set_text_fmt(s_lbl_bl,
                              "ROUND %lu/%d  -  backlight restored (GPIO%d=1)",
                              (unsigned long)(round + 1), BL_ROUNDS, BL_GPIO);
        lv_obj_set_style_text_color(s_lbl_bl, lv_color_hex(0xffd166), 0);
    }
}
#endif  // AI_PANEL_BL_TEST

// 100ms 心跳：刷新运行时长与进度条，证明 LVGL 任务持续被调度
static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    static uint32_t ticks;
    ticks++;

    lv_label_set_text_fmt(s_lbl_uptime, "UPTIME  %lu.%lu s",
                          (unsigned long)(ticks / 10), (unsigned long)(ticks % 10));

    // 0..100 来回扫，纯视觉心跳
    uint32_t phase = ticks % 200;
    lv_bar_set_value(s_bar_beat, (int32_t)(phase <= 100 ? phase : 200 - phase), LV_ANIM_OFF);

    // IMU 每 200ms 读一次（13 字节 I2C，很轻；别每帧读，没必要还占总线）
    // ⚠️ 这是**全工程唯一**读 I2C 的地方：i2c_master 句柄不能被多任务并发使用，
    //    否则总线状态机崩掉（实测 "clear bus failed" 刷屏，见 main.c 第 5 步注释）。
    if (ticks % 2 == 0) {
        if (imu_qmi8658_ok()) {
            imu_reading_t r;
            if (imu_qmi8658_read(&r) == ESP_OK) {
                // 每 5s 往串口打一次定量判据：|a| 静止应 ≈1000mg(定标正确)、陀螺应 ≈0
                if (ticks % 50 == 0) {
                    ESP_LOGI(TAG, "IMU addr=0x%02X whoami=0x%02X | a=(%.0f,%.0f,%.0f)mg "
                                  "|a|=%.0fmg | g=(%.2f,%.2f,%.2f)dps | %.1fC  "
                                  "[静止判据: |a|≈1000±50, |w|<2]",
                             imu_qmi8658_addr(), imu_qmi8658_whoami(),
                             r.ax_mg, r.ay_mg, r.az_mg, r.a_norm_mg,
                             r.gx_dps, r.gy_dps, r.gz_dps, r.temp_c);
                }
                lv_label_set_text_fmt(s_lbl_imu,
                                      "IMU  0x%02X ok  a=(%4d,%4d,%4d)mg  |a|=%4dmg  "
                                      "g=(%3d,%3d,%3d)dps  %2d C",
                                      imu_qmi8658_addr(),
                                      (int)r.ax_mg, (int)r.ay_mg, (int)r.az_mg, (int)r.a_norm_mg,
                                      (int)r.gx_dps, (int)r.gy_dps, (int)r.gz_dps, (int)r.temp_c);
            } else {
                lv_label_set_text(s_lbl_imu, "IMU  read FAILED");
            }
        } else {
            lv_label_set_text(s_lbl_imu, "IMU  NOT FOUND on I2C0 (SDA=7 SCL=8)");
        }
    }

    // SD 每 1s 刷一次（等 app_main 那边测速跑完）
    if (ticks % 10 == 0) {
        const sdcard_info_t *si = sdcard_info();
        const sdcard_bench_t *sb = sdcard_bench_result();
        if (!si->mounted) {
            lv_label_set_text(s_lbl_sd, "SD   not mounted - no card inserted?");
        } else if (sb->done) {
            lv_label_set_text_fmt(s_lbl_sd,
                                  "SD   %s  %llu MB  %d kHz  %d-line   |   "
                                  "write %.0f KB/s  read %.0f KB/s",
                                  si->name, (unsigned long long)si->size_mb,
                                  si->speed_khz, si->bus_width,
                                  sb->write_kbps, sb->read_kbps);
        } else {
            lv_label_set_text_fmt(s_lbl_sd, "SD   %s  %llu MB  %d kHz  %d-line   |   benchmarking...",
                                  si->name, (unsigned long long)si->size_mb,
                                  si->speed_khz, si->bus_width);
        }
    }
}

static void update_touch_label(void)
{
    if (s_last_x < 0) {
        lv_label_set_text(s_lbl_touch, "TOUCH   waiting for finger...");
    } else {
        lv_label_set_text_fmt(s_lbl_touch, "TOUCH   X=%4ld  Y=%4ld   presses=%lu",
                              (long)s_last_x, (long)s_last_y,
                              (unsigned long)s_press_count);
    }
}

static void touch_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        s_press_count++;
        lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        s_last_x = p.x;
        s_last_y = p.y;
        // 圆点跟手：直径 48，居中到手指位置
        lv_obj_set_pos(s_dot, p.x - 24, p.y - 24);
        update_touch_label();
        // 串口只在按下瞬间打一行，PRESSING 每帧都打会刷爆日志
        if (code == LV_EVENT_PRESSED) {
            ESP_LOGI(TAG, "touch #%lu at X=%ld Y=%ld",
                     (unsigned long)s_press_count, (long)p.x, (long)p.y);
        }
    }

    if (code == LV_EVENT_RELEASED) {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void ai_panel_create(void)
{
    // 面板初始化里已经把背光脚配成输出并拉高，这里再配一次（幂等），
    // 免得本模块依赖"上游一定配过"这个隐含假设。
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(BL_GPIO, 1);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f1a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 标题 ----
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-P4  6.2in MIPI-DSI");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ffc8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 18);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "AXS15260 452x1280 -> LVGL 1280x452 | PSRAM 32MB@200MHz | IDF v5.5.4");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8899aa), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 26, 56);

    // ---- 固件身份（证明是新烧的固件）----
    lv_obj_t *build = lv_label_create(scr);
    lv_label_set_text_fmt(build, "BUILD   %s  %s", __DATE__, __TIME__);
    lv_obj_set_style_text_font(build, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(build, lv_color_hex(0xffd166), 0);
    lv_obj_align(build, LV_ALIGN_TOP_LEFT, 26, 96);

    // ---- 运行时长 ----
    s_lbl_uptime = lv_label_create(scr);
    lv_label_set_text(s_lbl_uptime, "UPTIME  0.0 s");
    lv_obj_set_style_text_font(s_lbl_uptime, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl_uptime, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_lbl_uptime, LV_ALIGN_TOP_LEFT, 26, 136);

    // ---- 心跳条 ----
    s_bar_beat = lv_bar_create(scr);
    lv_obj_set_size(s_bar_beat, 1180, 14);
    lv_obj_align(s_bar_beat, LV_ALIGN_TOP_LEFT, 26, 180);
    lv_bar_set_range(s_bar_beat, 0, 100);
    lv_obj_set_style_bg_color(s_bar_beat, lv_color_hex(0x1b2436), 0);
    lv_obj_set_style_bg_color(s_bar_beat, lv_color_hex(0x00ffc8), LV_PART_INDICATOR);

    // ---- 触摸读数 ----
    s_lbl_touch = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_touch, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl_touch, lv_color_hex(0x7cd0ff), 0);
    lv_obj_align(s_lbl_touch, LV_ALIGN_TOP_LEFT, 26, 214);
    update_touch_label();

    // ---- 板载外设自检读数：QMI8658A 六轴 + microSD ----
    s_lbl_imu = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_imu, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_imu, lv_color_hex(0xb794f6), 0);
    lv_obj_align(s_lbl_imu, LV_ALIGN_TOP_LEFT, 26, 250);
    lv_label_set_text(s_lbl_imu, "IMU     init...");

    s_lbl_sd = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_sd, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_sd, lv_color_hex(0x7cd0ff), 0);
    lv_obj_align(s_lbl_sd, LV_ALIGN_TOP_LEFT, 26, 306);
    lv_label_set_text(s_lbl_sd, "SD      init...");

    // ---- 四角靶标：确认整块可视区都被画到（边缘被裁会立刻看出来）----
    const lv_color_t corner = lv_color_hex(0xff5c7a);
    const struct { lv_align_t a; } corners[] = {
        { LV_ALIGN_TOP_LEFT }, { LV_ALIGN_TOP_RIGHT },
        { LV_ALIGN_BOTTOM_LEFT }, { LV_ALIGN_BOTTOM_RIGHT },
    };
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); i++) {
        lv_obj_t *c = lv_obj_create(scr);
        lv_obj_set_size(c, 28, 28);
        lv_obj_set_style_radius(c, 0, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_bg_color(c, corner, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(c, corners[i].a, 0, 0);
    }

    // ---- 跟手圆点 ----
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, 48, 48);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xffd166), 0);
    lv_obj_set_style_border_width(s_dot, 2, 0);
    lv_obj_set_style_border_color(s_dot, lv_color_hex(0xffffff), 0);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);

    // 触摸事件挂在屏幕上：屏幕默认不可点击，要显式打开
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_RELEASED, NULL);

#if AI_PANEL_BL_TEST
    // ---- 背光判决测试的状态行（大字，放屏幕下方空白区）----
    s_lbl_bl = lv_label_create(scr);
    lv_label_set_text(s_lbl_bl, "BACKLIGHT TEST arming...");
    lv_obj_set_style_text_font(s_lbl_bl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_bl, lv_color_hex(0x8899aa), 0);
    lv_obj_align(s_lbl_bl, LV_ALIGN_TOP_LEFT, 26, 340);
    lv_timer_create(bl_test_cb, BL_TICK_MS, NULL);
#else
    // 背光归属已定论（GPIO30 真管背光，2026-07-27 真机确认），这里只留一行事实说明
    lv_obj_t *lbl_bl_fact = lv_label_create(scr);
    lv_label_set_text(lbl_bl_fact,
                      "VERIFIED: GPIO30 drives the backlight (screen went dark on cue).\n"
                      "Touch mapping exact - swap_xy/mirror_x/mirror_y all false is correct.");
    lv_obj_set_style_text_font(lbl_bl_fact, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_bl_fact, lv_color_hex(0x00ffc8), 0);
    lv_obj_align(lbl_bl_fact, LV_ALIGN_TOP_LEFT, 26, 340);
#endif

    lv_timer_create(tick_cb, 100, NULL);

    ESP_LOGI(TAG, "AI verification panel created (build %s %s), bl_test=%d",
             __DATE__, __TIME__, AI_PANEL_BL_TEST);
}
