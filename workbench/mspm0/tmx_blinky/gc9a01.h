/*
 * gc9a01.h - GC9A01 1.28" 240x240 round color TFT, MSPM0 driverlib version.
 * No framebuffer (MSPM0G3507 has only 32KB RAM): direct-draw over hardware SPI1.
 * Interface (天猛星 板载 SPI-LCD H8):
 *   SCL=PB9(SPI1_SCK)  SDA=PB8(SPI1_MOSI)  RES=PB10  DC=PB11  CS=PB14  BLK=PB26
 */
#ifndef GC9A01_H
#define GC9A01_H

#include <stdint.h>

#define LCD_W 240
#define LCD_H 240

/* RGB565 colors (normal order; byte-swap done on the wire in gc9a01.c) */
#define LCD_BLACK   0x0000
#define LCD_WHITE   0xFFFF
#define LCD_RED     0xF800
#define LCD_GREEN   0x07E0
#define LCD_BLUE    0x001F
#define LCD_YELLOW  0xFFE0
#define LCD_CYAN    0x07FF
#define LCD_MAGENTA 0xF81F
#define LCD_ORANGE  0xFD20
#define LCD_GRAY    0x8410

void GC9A01_Init(void);
void GC9A01_Backlight(uint8_t on);
void GC9A01_FillScreen(uint16_t color);
void GC9A01_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GC9A01_DrawPixel(int16_t x, int16_t y, uint16_t color);
/* 8x8 font, integer scale (scale=2 -> 16x16). bg==fg not special; use transparent=1 to skip bg. */
void GC9A01_DrawChar(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale);
void GC9A01_DrawString(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
/* centered string (by pixel width) at row y */
void GC9A01_DrawStringCentered(int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif /* GC9A01_H */
