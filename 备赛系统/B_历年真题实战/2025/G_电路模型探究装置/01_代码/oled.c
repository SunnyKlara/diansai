/**
 * @file    oled.c
 * @brief   SSD1306 OLED 实现（最小可用版）
 *
 *  本文件为 main.c 中的 OLED 调用提供功能性实现。
 *  设计目标：让工程能编译通过 + 结构清晰，赛场上可直接对接成熟的字模库。
 *
 *  字模占位：含 ASCII 0x20~0x7F 的 6×8 字模（这里用空字模框架），
 *           赛场上可以从开源仓库（如 oled_u8g2 / Atomthreads UI）粘贴完整字模。
 *
 *  通信：I2C 写入，控制字节 0x00 = 命令，0x40 = 数据。
 */

#include "oled.h"
#include <string.h>

/* ---- 模块状态 ---- */
static I2C_HandleTypeDef *s_hi2c = 0;
static uint8_t s_gram[8][128];     /* 显存：8 行 × 128 列 */

/* ---- 6×8 ASCII 字模（占位）----
 *  完整字模长度 96 × 6 = 576 字节。
 *  这里只放 ' '(空格) 和 'A' 两个示例（其余字符显示为空白），
 *  保证编译链接通过。赛场实操时替换为完整字模数组即可。
 */
static const uint8_t kFont6x8[][6] = {
    /* 0x20 ' ' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 0x21 '!' */ { 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00 },
};
#define FONT_FIRST_CHAR  0x20
#define FONT_NUM         (sizeof(kFont6x8) / 6)

/* ---- I2C 写入 ---- */
static void oled_write_cmd(uint8_t cmd)
{
    if (!s_hi2c) return;
    uint8_t buf[2] = { 0x00, cmd };
    HAL_I2C_Master_Transmit(s_hi2c, OLED_I2C_ADDR, buf, 2, 10);
}

static void oled_write_data(uint8_t data)
{
    if (!s_hi2c) return;
    uint8_t buf[2] = { 0x40, data };
    HAL_I2C_Master_Transmit(s_hi2c, OLED_I2C_ADDR, buf, 2, 10);
}

/* ---- 初始化序列 ----
 *  典型 SSD1306 128×64 初始化命令，节选自 SSD1306 datasheet p64
 */
static const uint8_t kInitSeq[] = {
    0xAE,                   /* Display OFF */
    0x20, 0x00,             /* Memory Mode: Horizontal */
    0xB0,                   /* Page start */
    0xC8,                   /* COM scan dir reverse */
    0x00,                   /* Lower column */
    0x10,                   /* Higher column */
    0x40,                   /* Start line */
    0x81, 0xCF,             /* Contrast */
    0xA1,                   /* Segment remap */
    0xA6,                   /* Normal display */
    0xA8, 0x3F,             /* Multiplex 1/64 */
    0xA4,                   /* Output follows RAM */
    0xD3, 0x00,             /* Display offset 0 */
    0xD5, 0xF0,             /* Clock div */
    0xD9, 0x22,             /* Pre-charge */
    0xDA, 0x12,             /* COM pins */
    0xDB, 0x20,             /* VCOMH */
    0x8D, 0x14,             /* Charge pump enable */
    0xAF,                   /* Display ON */
};

void OLED_Init(I2C_HandleTypeDef* hi2c)
{
    s_hi2c = hi2c;
    HAL_Delay(100);          /* 等 OLED 上电稳定 */

    for (uint32_t i = 0; i < sizeof(kInitSeq); i++) {
        oled_write_cmd(kInitSeq[i]);
    }

    OLED_Clear();
}

void OLED_Refresh(void)
{
    for (uint8_t page = 0; page < 8; page++) {
        oled_write_cmd(0xB0 | page);    /* 设置页 */
        oled_write_cmd(0x00);            /* 列低 4 位 */
        oled_write_cmd(0x10);            /* 列高 4 位 */
        for (uint8_t col = 0; col < 128; col++) {
            oled_write_data(s_gram[page][col]);
        }
    }
}

void OLED_Clear(void)
{
    memset(s_gram, 0, sizeof(s_gram));
    OLED_Refresh();
}

void OLED_ShowString(uint8_t x, uint8_t y, const char* str)
{
    if (!str) return;
    if (y > 7) return;

    while (*str && (x + 6) <= 128) {
        uint8_t ch = (uint8_t)*str;
        uint16_t idx = (ch >= FONT_FIRST_CHAR) ? (ch - FONT_FIRST_CHAR) : 0;
        if (idx >= FONT_NUM) idx = 0;   /* 字模缺失：显示为空格 */

        for (uint8_t c = 0; c < 6; c++) {
            s_gram[y][x + c] = kFont6x8[idx][c];
        }
        x += 6;
        str++;
    }
    OLED_Refresh();
}
