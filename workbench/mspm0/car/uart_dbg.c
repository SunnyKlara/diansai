/*
 * uart_dbg.c - 轻量调试串口输出 (UART0, 阻塞逐字节发送)
 * 引脚: TX=PA10 / RX=PA11 (板载 UART0, 接 DAP VCOM), 115200-8N1。
 * 只做 TX 打印(log): 逐字节阻塞发, 不依赖 printf/newlib, 省 flash 且时序可控。
 * 外设初始化在 SYSCFG_DL_init() 里 (syscfg 生成 DBG_UART_INST)。
 */
#include "ti_msp_dl_config.h"
#include "uart_dbg.h"

void uart_dbg_putc(char c)
{
    DL_UART_transmitDataBlocking(DBG_UART_INST, (uint8_t)c);
}

void uart_dbg_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            DL_UART_transmitDataBlocking(DBG_UART_INST, (uint8_t)'\r');
        DL_UART_transmitDataBlocking(DBG_UART_INST, (uint8_t)*s++);
    }
}

void uart_dbg_put_int(int32_t v)
{
    char buf[12];
    int n = 0;
    uint32_t a;

    if (v < 0) { uart_dbg_putc('-'); a = (uint32_t)(-(v + 1)) + 1u; }  /* 防 INT_MIN 溢出 */
    else       { a = (uint32_t)v; }

    if (a == 0) { uart_dbg_putc('0'); return; }
    while (a) { buf[n++] = (char)('0' + (a % 10u)); a /= 10u; }
    while (n) uart_dbg_putc(buf[--n]);
}
