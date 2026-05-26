/**
 * @file    oled.c
 * @brief   STM32G474 + I2C1 + SSD1306 OLED 0.96"
 *
 * 资源映射：I2C1 SCL=PB8, SDA=PB9, 100kHz
 *
 * 实现策略：
 *   - 不带字模库直接发命令，节省 Flash
 *   - 调用方提供小型字模（5x7 ASCII）即可
 *   - 16 × 8 字符屏（可见 4 行 × 16 字符）
 *
 * 字模来源：从开源项目（u8g2 / ssd1306 driver）拷一份 5x7 字模即可。
 * 本驱动仅暴露文本接口，不强制具体字模实现。
 */

#include "oled.h"
#include "stm32g4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define SSD1306_ADDR  (0x3C << 1)   /* HAL I2C 7-bit << 1 */

/* 简易 5x7 ASCII 字模（仅 0~9, A~Z, 空格, 部分符号；约 60 字符） */
static const uint8_t font5x7_ascii_dot[][5] = {
    {0x00,0x00,0x00,0x00,0x00},  /* SP */
    {0x3E,0x51,0x49,0x45,0x3E},  /* 0 */
    {0x00,0x42,0x7F,0x40,0x00},  /* 1 */
    {0x42,0x61,0x51,0x49,0x46},  /* 2 */
    {0x21,0x41,0x45,0x4B,0x31},  /* 3 */
    {0x18,0x14,0x12,0x7F,0x10},  /* 4 */
    {0x27,0x45,0x45,0x45,0x39},  /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30},  /* 6 */
    {0x01,0x71,0x09,0x05,0x03},  /* 7 */
    {0x36,0x49,0x49,0x49,0x36},  /* 8 */
    {0x06,0x49,0x49,0x29,0x1E},  /* 9 */
};
/* 仅占位：完整字模请用 u8g2 或 ssd1306 库替换。 */

static uint8_t font_idx_of(char c)
{
    if (c == ' ') return 0;
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 1);
    return 0;   /* 其他字符显示空格 */
}

static void send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, buf, 2, 10);
}

static void send_data(const uint8_t *data, uint16_t len)
{
    /* 控制字节 0x40 + N 字节数据 */
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

void OLED_Init(void)
{
    HAL_Delay(50);

    static const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF
    };
    for (uint16_t i = 0; i < sizeof(init_seq); i++) send_cmd(init_seq[i]);
}

void OLED_Clear(void)
{
    static uint8_t blank[128];
    memset(blank, 0, sizeof(blank));
    for (uint8_t page = 0; page < 8; page++) {
        set_pos(0, page);
        send_data(blank, 128);
    }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    if (!str) return;
    set_pos(x * 6, y);   /* 每字符 6 px 宽（5 + 1 间距） */
    while (*str) {
        uint8_t buf[6];
        uint8_t idx = font_idx_of(*str);
        for (uint8_t k = 0; k < 5; k++) buf[k] = font5x7_ascii_dot[idx][k];
        buf[5] = 0x00;
        send_data(buf, 6);
        str++;
    }
}

void OLED_ShowFloat(uint8_t x, uint8_t y, const char *label, float val, const char *unit)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%6.2f%s",
             label ? label : "",
             (double)val,
             unit ? unit : "");
    OLED_ShowString(x, y, buf);
}
