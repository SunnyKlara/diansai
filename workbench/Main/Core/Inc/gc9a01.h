#ifndef __GC9A01_H
#define __GC9A01_H

/*
 * gc9a01.h - GC9A01 1.28" round color TFT driver (240x240, RGB565)
 *
 * Register-level hardware SPI1 (no HAL SPI dependency), full framebuffer
 * + region flush. Reuses fonts from oled_font.h (asc2_*) / cn_font.h (cn16)
 * via extern only (instantiated once in oled.c, see CN_FONT_IMPL).
 *
 * Wiring (see config.h): SCL=PB3 SDA=PB5 DC=PG14 CS=PG13 RST=PE6.
 *
 * NOTE: ASCII-only on purpose. armclang here decodes sources as GBK; UTF-8
 * CJK comments abutting a comment terminator break the build.
 */

#include <stdint.h>
#include "config.h"

/* SPI polling-timeout counter; 0 = SPI transmitting fine, large = SPI dead. */
extern volatile uint32_t g_lcd_spi_to;

/* ---- Colors (RGB565, byte-swapped for 8-bit MSB-first SPI stream) ---- */
#define LCD_SWAP(c)   ((uint16_t)(((c) >> 8) | ((c) << 8)))
#define LCD_BLACK     0x0000
#define LCD_WHITE     0xFFFF
#define LCD_RED       LCD_SWAP(0xF800)
#define LCD_GREEN     LCD_SWAP(0x07E0)
#define LCD_BLUE      LCD_SWAP(0x001F)
#define LCD_YELLOW    LCD_SWAP(0xFFE0)
#define LCD_CYAN      LCD_SWAP(0x07FF)
#define LCD_MAGENTA   LCD_SWAP(0xF81F)
#define LCD_ORANGE    LCD_SWAP(0xFD20)
#define LCD_GRAY      LCD_SWAP(0x8410)
#define LCD_DGRAY     LCD_SWAP(0x39E7)
#define LCD_LGRAY     LCD_SWAP(0xBDF7)

/* Build a byte-swapped RGB565 from 8/8/8 (runtime custom colors) */
static inline uint16_t LCD_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

/* ---- Init / flush ---- */
void GC9A01_Init(void);
void GC9A01_FillScreen(uint16_t color);                 /* solid fill, bypass framebuffer */void GC9A01_Flush(void);                                /* push whole framebuffer */
void GC9A01_FlushRegion(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* ---- Framebuffer primitives (write to RAM, need Flush to show) ---- */
void GC9A01_Clear(uint16_t color);
void GC9A01_DrawPixel(int16_t x, int16_t y, uint16_t color);
void GC9A01_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GC9A01_DrawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
void GC9A01_DrawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
void GC9A01_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GC9A01_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void GC9A01_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);

/* Text: size=8/12/16/24 (ASCII). transparent!=0 skips background. */
void GC9A01_DrawChar(int16_t x, int16_t y, char ch, uint8_t size,
                     uint16_t fg, uint16_t bg, uint8_t transparent);
void GC9A01_DrawString(int16_t x, int16_t y, const char *s, uint8_t size,
                       uint16_t fg, uint16_t bg, uint8_t transparent);
/* 16x16 Chinese glyph (idx = CN_xxx from cn_font.h) */
void GC9A01_DrawCN16(int16_t x, int16_t y, uint8_t idx,
                     uint16_t fg, uint16_t bg, uint8_t transparent);
void GC9A01_DrawCNStr(int16_t x, int16_t y, const uint8_t *idx, uint8_t n,
                      uint16_t fg, uint16_t bg, uint8_t transparent);

#endif /* __GC9A01_H */
