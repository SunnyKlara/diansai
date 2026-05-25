/**
 * @file    lcd_tft.c
 * @brief   2.8" SPI TFT 显示驱动占位
 *
 * 真机移植：替换 _hal_send 系列为 SPI 发送 + DC/CS GPIO。
 */

#include "lcd_tft.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * 占位钩子
 * ===================================================================== */

static void _hal_send_cmd(uint8_t cmd)   { (void)cmd; }
static void _hal_send_data(uint8_t data) { (void)data; }
static void _hal_fill_color(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    (void)x; (void)y; (void)w; (void)h; (void)c;
}
static void _hal_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t c)
{
    (void)x; (void)y; (void)s; (void)c;
}

/* ========================================================================
 * API
 * ===================================================================== */

void lcd_tft_init(void)
{
    _hal_send_cmd(0x01);  /* SW reset */
    /* TODO: ILI9341 初始化序列 + 方向设置 */
}

void lcd_tft_clear(void)
{
    _hal_fill_color(0, 0, 320, 240, 0x0000);
}

void lcd_tft_show_mode(const char *mode_name)
{
    if (mode_name == 0) return;
    _hal_fill_color(0, 0, 320, 20, 0x001F);   /* 顶部蓝条 */
    _hal_draw_text(8, 4, mode_name, 0xFFFF);
}

void lcd_tft_show_wave(const float *us_buf, const float *il_buf,
                       const float *is_buf, uint16_t w)
{
    if ((us_buf == 0) || (il_buf == 0) || (is_buf == 0)) return;
    /* 中间 320×120 区域：3 条曲线叠加 */
    _hal_fill_color(0, 24, 320, 120, 0x0000);
    /* TODO: drawLine for each (x, value) */
    (void)w;
}

void lcd_tft_show_harmonic(const harmonic_result_t *r)
{
    if (r == 0) return;

    char line[64];
    /* 底部行 1：基波 + RMS + THD */
    snprintf(line, sizeof(line), "I1=%.3fA  Irms=%.3fA  THD=%.1f%%",
             (double)r->i_fund_rms, (double)r->i_rms, (double)r->thd_pct);
    _hal_draw_text(4, 152, line, 0xFFFF);

    /* 底部行 2：H₂..H₅ */
    snprintf(line, sizeof(line), "H2=%.1f%%  H3=%.1f%%  H4=%.1f%%  H5=%.1f%%",
             (double)r->Hi_pct[2], (double)r->Hi_pct[3],
             (double)r->Hi_pct[4], (double)r->Hi_pct[5]);
    _hal_draw_text(4, 176, line, 0xFFFF);
}
