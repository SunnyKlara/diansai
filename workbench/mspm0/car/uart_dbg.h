#ifndef UART_DBG_H
#define UART_DBG_H

#include <stdint.h>

/*
 * 轻量调试串口输出 (UART0 @115200-8N1, TX=PA10 / RX=PA11, 接 DAP VCOM)。
 * 只做 TX 打印(log), 逐字节阻塞发, 不依赖 printf/newlib, 省 flash。
 * UART 外设本身由 SYSCFG_DL_init() 初始化, 这里只封装发送。
 */
void uart_dbg_putc(char c);
void uart_dbg_puts(const char *s);   /* 遇 '\n' 自动补 '\r', 串口助手换行整齐 */
void uart_dbg_put_int(int32_t v);    /* 带符号十进制 */

#endif /* UART_DBG_H */
