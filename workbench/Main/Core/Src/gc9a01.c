/*
 * gc9a01.c - GC9A01 1.28" round color TFT, register-level hardware SPI1.
 *
 * Design choices (fit this project's constraints):
 *  - stm32h7xx_hal_spi.c is MISSING from this project, so SPI1 is driven by
 *    direct registers (SPIv2: CFG1/CFG2/CR1/CR2), no HAL SPI dependency.
 *  - No DMA (hand-rolled H7 SPIv2+DMA is risky without hardware); CPU polling
 *    transmit + GC9A01_FlushRegion() (scope area only) keep the per-frame
 *    blocking acceptable.
 *  - framebuffer stores byte-swapped RGB565 so the 8-bit SPI byte stream is
 *    MSB-first (no per-pixel swap). 115KB lives in AXI SRAM (CPU access only,
 *    no cache maintenance needed).
 *
 * Fonts: reuse asc2_x and cn16 instantiated in oled.c (extern), no flash dup.
 *
 * Status: NOT yet tested on hardware. Run the bring-up checklist in
 * 04 debug log before trusting the picture.
 *
 * ASCII-only on purpose (armclang decodes sources as GBK here).
 */

#include "stm32h7xx_hal.h"
#include "gc9a01.h"
#include "cn_font.h"          /* CN_xxx macros + extern cn16 (no CN_FONT_IMPL) */

/* ---- fonts instantiated in oled.c (external linkage) ---- */
extern const unsigned char asc2_0806[][6];
extern const unsigned char asc2_1206[][12];
extern const unsigned char asc2_1608[][16];
extern const unsigned char asc2_2412[][36];

/* ====================== control pins (GPIO BSRR, atomic) ====================== */
/* ACTUAL WIRING (matches the board as wired):
 *   DC = PG13   CS = PE6   RST = PG14
 * (all three already configured as push-pull outputs by MX_GPIO_Init) */
#define CS_L()   (GPIOE->BSRR = (uint32_t)GPIO_PIN_6 << 16)
#define CS_H()   (GPIOE->BSRR = GPIO_PIN_6)
#define DC_CMD() (GPIOG->BSRR = (uint32_t)GPIO_PIN_13 << 16)   /* DC=0 command */
#define DC_DAT() (GPIOG->BSRR = GPIO_PIN_13)                   /* DC=1 data */
#define RST_L()  (GPIOG->BSRR = (uint32_t)GPIO_PIN_14 << 16)
#define RST_H()  (GPIOG->BSRR = GPIO_PIN_14)

/* ====================== framebuffer (AXI SRAM, CPU only) ====================== */
static uint16_t s_fb[LCD_W * LCD_H];

/* ====================== register-level SPI1 ====================== */
static void spi1_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB3=SPI1_SCK, PB5=SPI1_MOSI to AF5 (override the GPIO-out set by MX_GPIO_Init) */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_3 | GPIO_PIN_5;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &g);

    /* SPI1 kernel clock = pll1_q_ck (000), PROVEN running at 240MHz here.
     *
     * SPI123SEL encoding (RM0433):
     *   000 pll1_q_ck  001 pll2_p_ck  010 pll3_p_ck
     *   011 I2S_CKIN (external pin!)  100 per_ck
     * History: code first used 011 thinking it was per_ck -> 011 is the
     * UNCONNECTED I2S_CKIN pin -> kernel clock 0 -> TXP never sets (huge LCDto).
     * per_ck (100) needs HSI left on, which is only probable here, not certain.
     * pll1_q is the safe pick: SystemClock_Config runs SYSCLK from PLL1P (480MHz,
     * so PLL1 is locked/on) and HAL_RCC_OscConfig enables DIVQ1EN unconditionally,
     * so pll1_q = HSE25/M5*N192/Q4 = 240MHz is guaranteed live. SCK = 240/16 = 15MHz. */
    RCC->D2CCIP1R &= ~RCC_D2CCIP1R_SPI123SEL;              /* 000 = pll1_q_ck (240MHz) */

    /* ---- CRITICAL ORDER (MODF avoidance) ----
     * In master mode with software NSS, the internal NSS must be HIGH before the
     * MASTER bit is set, otherwise the hardware sees NSS low, raises a Mode Fault
     * (MODF), and AUTO-CLEARS MASTER + SPE. Symptom: SPISR bit9(MODF)=1,
     * CFG2 MASTER=0, SPE never holds, TXP never sets, every byte times out.
     * So: set SSI(=1) FIRST, then write CFG2 with MASTER. The SR read + CFG2 write
     * also clears any stale MODF latched from a previous run. */
    SPI1->CR1  = SPI_CR1_SSI;                          /* SSI=1: internal NSS HIGH first */
    SPI1->CFG1 = (7u << SPI_CFG1_DSIZE_Pos)            /* 8-bit frame (DSIZE=8-1) */
               | ((uint32_t)LCD_SPI_MBR << SPI_CFG1_MBR_Pos);
    (void)SPI1->SR;                                    /* SR read: part of MODF clear seq */
    SPI1->CFG2 = SPI_CFG2_MASTER                       /* master (now sticks: NSS high) */
               | SPI_CFG2_SSM                          /* software NSS */
               | (1u << SPI_CFG2_COMM_Pos);            /* 01 = simplex transmitter */
    /* CPOL=0 CPHA=0 (mode 0), MSB first: all default */
}

/* polling send n bytes; TSIZE max 65535, chunk if longer.
 * Waits are bounded and short so a dead SPI can't stall init for long; every
 * bailout bumps g_lcd_spi_to, which the UART heartbeat prints -> direct
 * "is SPI transmitting?" diagnostic. */
#define SPI_WAIT_LIMIT  1000u
volatile uint32_t g_lcd_spi_to = 0;     /* count of polling timeouts (0 = SPI healthy) */
static void spi1_tx(const uint8_t *d, uint32_t n)
{
    while (n) {
        uint32_t chunk = (n > 65535u) ? 65535u : n;
        uint32_t g;

        SPI1->CR1 &= ~SPI_CR1_SPE;
        SPI1->CR2  = chunk;                             /* TSIZE */
        SPI1->CR1 |= SPI_CR1_SPE;
        SPI1->CR1 |= SPI_CR1_CSTART;

        for (uint32_t i = 0; i < chunk; i++) {
            g = SPI_WAIT_LIMIT;
            while (!(SPI1->SR & SPI_SR_TXP) && --g) { }
            if (!g) g_lcd_spi_to++;
            *(volatile uint8_t *)&SPI1->TXDR = d[i];
        }
        g = SPI_WAIT_LIMIT;
        while (!(SPI1->SR & SPI_SR_EOT) && --g) { }
        if (!g) g_lcd_spi_to++;
        SPI1->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
        SPI1->CR1 &= ~SPI_CR1_SPE;

        d += chunk;
        n -= chunk;
    }
}

static void wr_cmd(uint8_t c)
{
    DC_CMD(); CS_L();
    spi1_tx(&c, 1);
    CS_H();
}

static void wr_dat(uint8_t v)
{
    DC_DAT(); CS_L();
    spi1_tx(&v, 1);
    CS_H();
}

/* ====================== GC9A01 init sequence ====================== */
/* Standard GC9A01(A) vendor init; works for generic modules. */
static void gc9a01_reset(void)
{
    RST_H(); HAL_Delay(10);
    RST_L(); HAL_Delay(20);
    RST_H(); HAL_Delay(120);
}

static void gc9a01_init_seq(void)
{
    wr_cmd(0xEF);
    wr_cmd(0xEB); wr_dat(0x14);
    wr_cmd(0xFE);
    wr_cmd(0xEF);
    wr_cmd(0xEB); wr_dat(0x14);
    wr_cmd(0x84); wr_dat(0x40);
    wr_cmd(0x85); wr_dat(0xFF);
    wr_cmd(0x86); wr_dat(0xFF);
    wr_cmd(0x87); wr_dat(0xFF);
    wr_cmd(0x88); wr_dat(0x0A);
    wr_cmd(0x89); wr_dat(0x21);
    wr_cmd(0x8A); wr_dat(0x00);
    wr_cmd(0x8B); wr_dat(0x80);
    wr_cmd(0x8C); wr_dat(0x01);
    wr_cmd(0x8D); wr_dat(0x01);
    wr_cmd(0x8E); wr_dat(0xFF);
    wr_cmd(0x8F); wr_dat(0xFF);
    wr_cmd(0xB6); wr_dat(0x00); wr_dat(0x20);
    wr_cmd(0x36); wr_dat(LCD_MADCTL);                   /* orientation */
    wr_cmd(0x3A); wr_dat(0x05);                         /* 16bit/pixel RGB565 */
    wr_cmd(0x90); wr_dat(0x08); wr_dat(0x08); wr_dat(0x08); wr_dat(0x08);
    wr_cmd(0xBD); wr_dat(0x06);
    wr_cmd(0xBC); wr_dat(0x00);
    wr_cmd(0xFF); wr_dat(0x60); wr_dat(0x01); wr_dat(0x04);
    wr_cmd(0xC3); wr_dat(0x13);
    wr_cmd(0xC4); wr_dat(0x13);
    wr_cmd(0xC9); wr_dat(0x22);
    wr_cmd(0xBE); wr_dat(0x11);
    wr_cmd(0xE1); wr_dat(0x10); wr_dat(0x0E);
    wr_cmd(0xDF); wr_dat(0x21); wr_dat(0x0C); wr_dat(0x02);
    wr_cmd(0xF0); wr_dat(0x45); wr_dat(0x09); wr_dat(0x08); wr_dat(0x08); wr_dat(0x26); wr_dat(0x2A);
    wr_cmd(0xF1); wr_dat(0x43); wr_dat(0x70); wr_dat(0x72); wr_dat(0x36); wr_dat(0x37); wr_dat(0x6F);
    wr_cmd(0xF2); wr_dat(0x45); wr_dat(0x09); wr_dat(0x08); wr_dat(0x08); wr_dat(0x26); wr_dat(0x2A);
    wr_cmd(0xF3); wr_dat(0x43); wr_dat(0x70); wr_dat(0x72); wr_dat(0x36); wr_dat(0x37); wr_dat(0x6F);
    wr_cmd(0xED); wr_dat(0x1B); wr_dat(0x0B);
    wr_cmd(0xAE); wr_dat(0x77);
    wr_cmd(0xCD); wr_dat(0x63);
    wr_cmd(0x70); wr_dat(0x07); wr_dat(0x07); wr_dat(0x04); wr_dat(0x0E); wr_dat(0x0F); wr_dat(0x09); wr_dat(0x07); wr_dat(0x08); wr_dat(0x03);
    wr_cmd(0xE8); wr_dat(0x34);
    wr_cmd(0x62); wr_dat(0x18); wr_dat(0x0D); wr_dat(0x71); wr_dat(0xED); wr_dat(0x70); wr_dat(0x70);
                  wr_dat(0x18); wr_dat(0x0F); wr_dat(0x71); wr_dat(0xEF); wr_dat(0x70); wr_dat(0x70);
    wr_cmd(0x63); wr_dat(0x18); wr_dat(0x11); wr_dat(0x71); wr_dat(0xF1); wr_dat(0x70); wr_dat(0x70);
                  wr_dat(0x18); wr_dat(0x13); wr_dat(0x71); wr_dat(0xF3); wr_dat(0x70); wr_dat(0x70);
    wr_cmd(0x64); wr_dat(0x28); wr_dat(0x29); wr_dat(0xF1); wr_dat(0x01); wr_dat(0xF1); wr_dat(0x00); wr_dat(0x07);
    wr_cmd(0x66); wr_dat(0x3C); wr_dat(0x00); wr_dat(0xCD); wr_dat(0x67); wr_dat(0x45); wr_dat(0x45); wr_dat(0x10); wr_dat(0x00); wr_dat(0x00); wr_dat(0x00);
    wr_cmd(0x67); wr_dat(0x00); wr_dat(0x3C); wr_dat(0x00); wr_dat(0x00); wr_dat(0x00); wr_dat(0x01); wr_dat(0x54); wr_dat(0x10); wr_dat(0x32); wr_dat(0x98);
    wr_cmd(0x74); wr_dat(0x10); wr_dat(0x85); wr_dat(0x80); wr_dat(0x00); wr_dat(0x00); wr_dat(0x4E); wr_dat(0x00);
    wr_cmd(0x98); wr_dat(0x3E); wr_dat(0x07);
    wr_cmd(0x35); wr_dat(0x00);                         /* TE line on (ok even if TE unwired) */
    wr_cmd(0x21);                                       /* display inversion on (GC9A01 norm) */
    wr_cmd(0x11);                                       /* sleep out */
    HAL_Delay(120);
    wr_cmd(0x29);                                       /* display on */
    HAL_Delay(20);
}

/* set GRAM window and issue RAMWR(0x2C); then stream pixels */
static void gc9a01_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    wr_cmd(0x2A);
    DC_DAT(); CS_L();
    { uint8_t b[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 }; spi1_tx(b, 4); }
    CS_H();
    wr_cmd(0x2B);
    DC_DAT(); CS_L();
    { uint8_t b[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 }; spi1_tx(b, 4); }
    CS_H();
    wr_cmd(0x2C);
}

void GC9A01_Init(void)
{
    spi1_init();
    gc9a01_reset();
    gc9a01_init_seq();
#if LCD_BOOT_SELFTEST
    GC9A01_FillScreen(LCD_RED);   HAL_Delay(400);
    GC9A01_FillScreen(LCD_GREEN); HAL_Delay(400);
    GC9A01_FillScreen(LCD_BLUE);  HAL_Delay(400);
#endif
    GC9A01_FillScreen(LCD_BLACK);
}

/* solid color, bypass framebuffer (power-on test / clear) */
void GC9A01_FillScreen(uint16_t color)
{
    gc9a01_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    DC_DAT(); CS_L();
    uint8_t hi = (uint8_t)(color & 0xFF);              /* color is already in store order */
    uint8_t lo = (uint8_t)(color >> 8);
    static uint8_t line[LCD_W * 2];
    for (uint16_t i = 0; i < LCD_W; i++) { line[i * 2] = hi; line[i * 2 + 1] = lo; }
    for (uint16_t y = 0; y < LCD_H; y++) spi1_tx(line, sizeof(line));
    CS_H();
}

void GC9A01_Flush(void)
{
    gc9a01_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    DC_DAT(); CS_L();
    spi1_tx((const uint8_t *)s_fb, (uint32_t)LCD_W * LCD_H * 2u);
    CS_H();
}

void GC9A01_FlushRegion(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (x1 >= LCD_W) x1 = LCD_W - 1;
    if (y1 >= LCD_H) y1 = LCD_H - 1;
    if (x0 > x1 || y0 > y1) return;
    gc9a01_set_window(x0, y0, x1, y1);
    DC_DAT(); CS_L();
    uint16_t w = (uint16_t)(x1 - x0 + 1);
    for (uint16_t y = y0; y <= y1; y++)
        spi1_tx((const uint8_t *)&s_fb[(uint32_t)y * LCD_W + x0], (uint32_t)w * 2u);
    CS_H();
}

/* ====================== framebuffer primitives ====================== */
void GC9A01_Clear(uint16_t color)
{
    for (uint32_t i = 0; i < (uint32_t)LCD_W * LCD_H; i++) s_fb[i] = color;
}

void GC9A01_DrawPixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    s_fb[(uint32_t)y * LCD_W + x] = color;
}

void GC9A01_DrawHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    if (y < 0 || y >= LCD_H || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (w <= 0) return;
    uint16_t *p = &s_fb[(uint32_t)y * LCD_W + x];
    while (w--) *p++ = color;
}

void GC9A01_DrawVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    if (x < 0 || x >= LCD_W || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > LCD_H) h = LCD_H - y;
    if (h <= 0) return;
    uint16_t *p = &s_fb[(uint32_t)y * LCD_W + x];
    while (h--) { *p = color; p += LCD_W; }
}

void GC9A01_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    for (int16_t r = 0; r < h; r++) GC9A01_DrawHLine(x, y + r, w, color);
}

void GC9A01_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    GC9A01_DrawHLine(x, y, w, color);
    GC9A01_DrawHLine(x, y + h - 1, w, color);
    GC9A01_DrawVLine(x, y, h, color);
    GC9A01_DrawVLine(x + w - 1, y, h, color);
}

void GC9A01_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (dx > dy ? dx : -dy) / 2, e2;
    for (;;) {
        GC9A01_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

void GC9A01_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
    int16_t x = -r, y = 0, err = 2 - 2 * r;
    do {
        GC9A01_DrawPixel(cx - x, cy + y, color);
        GC9A01_DrawPixel(cx - y, cy - x, color);
        GC9A01_DrawPixel(cx + x, cy - y, color);
        GC9A01_DrawPixel(cx + y, cy + x, color);
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}

/* ====================== text (mirror oled.c glyph order: column scan, LSB top) ====================== */
static uint8_t font_byte(uint8_t size, uint16_t chr1, uint16_t i)
{
    switch (size) {
        case 8:  return asc2_0806[chr1][i];
        case 12: return asc2_1206[chr1][i];
        case 16: return asc2_1608[chr1][i];
        case 24: return asc2_2412[chr1][i];
        default: return 0;
    }
}

void GC9A01_DrawChar(int16_t x, int16_t y, char ch, uint8_t size,
                     uint16_t fg, uint16_t bg, uint8_t transparent)
{
    if (ch < ' ' || ch > '~') return;
    uint16_t chr1 = (uint16_t)(ch - ' ');
    uint8_t  size2 = (size == 8) ? 6
                   : (uint8_t)((size / 8 + ((size % 8) ? 1 : 0)) * (size / 2));
    int16_t x0 = x, y0 = y;
    for (uint8_t i = 0; i < size2; i++) {
        uint8_t temp = font_byte(size, chr1, i);
        for (uint8_t m = 0; m < 8; m++) {
            if (temp & 0x01)            GC9A01_DrawPixel(x, y, fg);
            else if (!transparent)      GC9A01_DrawPixel(x, y, bg);
            temp >>= 1;
            y++;
        }
        x++;
        if (size != 8 && (x - x0) == size / 2) { x = x0; y0 += 8; }
        y = y0;
    }
}

void GC9A01_DrawString(int16_t x, int16_t y, const char *s, uint8_t size,
                       uint16_t fg, uint16_t bg, uint8_t transparent)
{
    while (*s >= ' ' && *s <= '~') {
        GC9A01_DrawChar(x, y, *s, size, fg, bg, transparent);
        x += (size == 8) ? 6 : (size / 2);
        s++;
    }
}

void GC9A01_DrawCN16(int16_t x, int16_t y, uint8_t idx,
                     uint16_t fg, uint16_t bg, uint8_t transparent)
{
    int16_t x0 = x, y0 = y;
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t temp = cn16[idx][i];
        for (uint8_t m = 0; m < 8; m++) {
            if (temp & 0x01)            GC9A01_DrawPixel(x, y, fg);
            else if (!transparent)      GC9A01_DrawPixel(x, y, bg);
            temp >>= 1;
            y++;
        }
        x++;
        if ((x - x0) == 16) { x = x0; y0 += 8; }
        y = y0;
    }
}

void GC9A01_DrawCNStr(int16_t x, int16_t y, const uint8_t *idx, uint8_t n,
                      uint16_t fg, uint16_t bg, uint8_t transparent)
{
    for (uint8_t i = 0; i < n; i++)
        GC9A01_DrawCN16(x + i * 16, y, idx[i], fg, bg, transparent);
}
