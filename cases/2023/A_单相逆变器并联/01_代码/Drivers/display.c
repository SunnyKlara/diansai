/**
 * @file    display.c
 * @brief   STM32G474 + I2C1 + SSD1306 OLED 0.96"
 *
 * 资源映射：I2C1 SCL=PB8, SDA=PB9, 100kHz
 *
 * 字模：随驱动给出 5x7 数字字模骨架；完整 ASCII 字模请自行从 u8g2 / ssd1306
 *      公开仓库拷贝（标准 5×7 一份 768 字节）。
 */

#include "display.h"
#include "../config.h"
#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define SSD1306_ADDR (0x3C << 1)

/* 5×7 简易字模：'0'~'9' + 空格 + 部分符号 */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},  /* SP    idx 0 */
    {0x3E,0x51,0x49,0x45,0x3E},  /* 0     idx 1 */
    {0x00,0x42,0x7F,0x40,0x00},  /* 1     idx 2 */
    {0x42,0x61,0x51,0x49,0x46},  /* 2     idx 3 */
    {0x21,0x41,0x45,0x4B,0x31},  /* 3     idx 4 */
    {0x18,0x14,0x12,0x7F,0x10},  /* 4     idx 5 */
    {0x27,0x45,0x45,0x45,0x39},  /* 5     idx 6 */
    {0x3C,0x4A,0x49,0x49,0x30},  /* 6     idx 7 */
    {0x01,0x71,0x09,0x05,0x03},  /* 7     idx 8 */
    {0x36,0x49,0x49,0x49,0x36},  /* 8     idx 9 */
    {0x06,0x49,0x49,0x29,0x1E},  /* 9     idx 10 */
    {0x00,0x36,0x36,0x00,0x00},  /* :     idx 11 */
    {0x00,0x60,0x60,0x00,0x00},  /* .     idx 12 */
    {0x08,0x08,0x08,0x08,0x08},  /* -     idx 13 */
};

static uint8_t font_idx_of(char c)
{
    if (c == ' ') return 0;
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 1);
    if (c == ':') return 11;
    if (c == '.') return 12;
    if (c == '-') return 13;
    return 0;
}

static void send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, buf, 2, 10);
}

static void send_data(const uint8_t *data, uint16_t len)
{
    uint8_t hdr = 0x40;
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, &hdr, 1, 10);
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, (uint8_t *)data, len, 100);
}

static void set_pos(uint8_t col, uint8_t page)
{
    send_cmd(0xB0 | (page & 0x07));
    send_cmd(0x00 | (col & 0x0F));
    send_cmd(0x10 | ((col >> 4) & 0x0F));
}

void Display_Init(void)
{
    HAL_Delay(50);
    static const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF
    };
    for (uint16_t i = 0; i < sizeof(init_seq); i++) send_cmd(init_seq[i]);
    Display_Clear();
}

void Display_Clear(void)
{
    static uint8_t blank[128];
    memset(blank, 0, sizeof(blank));
    for (uint8_t page = 0; page < 8; page++) {
        set_pos(0, page);
        send_data(blank, 128);
    }
}

void Display_ShowString(uint8_t x, uint8_t y, const char *str)
{
    if (!str) return;
    set_pos(x * 6, y);
    while (*str) {
        uint8_t buf[6];
        uint8_t idx = font_idx_of(*str);
        for (uint8_t k = 0; k < 5; k++) buf[k] = font5x7[idx][k];
        buf[5] = 0x00;
        send_data(buf, 6);
        str++;
    }
}

void Display_ShowFloat(uint8_t x, uint8_t y, const char *label, float val, const char *unit)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%6.2f%s",
             label ? label : "",
             (double)val,
             unit ? unit : "");
    Display_ShowString(x, y, buf);
}
