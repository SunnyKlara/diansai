/*
 * 天猛星 GC9A01 圆屏点屏 demo (MSPM0G3507).
 * 流程: 上电 -> 背光ON -> 自检刷 红/绿/蓝 全屏 -> 画 UI(标题/芯片名/LCD OK) -> 计数器循环刷新 + 状态灯心跳.
 * 屏接板载 SPI-LCD 接口(H8): SCL=PB9 SDA=PB8 RES=PB10 DC=PB11 CS=PB14 BLK=PB26.
 */
#include "ti_msp_dl_config.h"
#include "gc9a01.h"

#define CPUCLK_HZ 32000000UL
static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

/* 无符号整数 -> 十进制字符串 (避免拉入 printf) */
static void u2str(uint32_t v, char *buf)
{
    char tmp[11]; int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v && i < 10) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}

int main(void)
{
    SYSCFG_DL_init();

    GC9A01_Backlight(1);          /* 背光先开,便于观察 */
    GC9A01_Init();

    /* ---- 自检: 三色全屏,证明 SPI+面板 通路 ---- */
    GC9A01_FillScreen(LCD_RED);   delay_ms(400);
    GC9A01_FillScreen(LCD_GREEN); delay_ms(400);
    GC9A01_FillScreen(LCD_BLUE);  delay_ms(400);
    GC9A01_FillScreen(LCD_BLACK); delay_ms(100);

    /* ---- 静态 UI (用 白/绿, 不受 BGR 红蓝互换影响) ---- */
    GC9A01_DrawStringCentered(40,  "TIANMENGXING", LCD_GREEN, LCD_BLACK, 2);
    GC9A01_DrawStringCentered(72,  "MSPM0G3507",   LCD_WHITE, LCD_BLACK, 2);
    GC9A01_DrawStringCentered(104, "LCD OK",       LCD_WHITE, LCD_BLACK, 3);

    uint32_t n = 0;
    char buf[16];
    while (1) {
        /* 计数器: 每次清一小条再画,避免残影 */
        GC9A01_FillRect(30, 150, 180, 24, LCD_BLACK);
        buf[0] = 'N'; buf[1] = '=';
        u2str(n, &buf[2]);
        GC9A01_DrawStringCentered(150, buf, LCD_CYAN, LCD_BLACK, 2);

        DL_GPIO_togglePins(GPIO_LCD_PORT, GPIO_LCD_STATUS_LED_PIN);  /* PB22 心跳 */
        n++;
        delay_ms(300);
    }
}
