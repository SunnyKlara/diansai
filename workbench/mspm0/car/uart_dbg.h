#ifndef UART_DBG_H
#define UART_DBG_H

#include <stdint.h>

/*
 * 轻量调试输出：把同一份文本同时发到**两个**串口(可运行时选)。
 *   sink DAP = UART0 @115200-8N1, TX=PA10 / RX=PA11 -> 板载 DAP 的 VCOM(有线 COM 口)
 *   sink ESP = UART3 @115200-8N1, TX=PB2  / RX=PB3  -> 车载 ESP-01S(无线桥) [CFG_ESP_UART_EN]
 *
 * 只做 TX 打印(log)，逐字节阻塞发，不依赖 printf/newlib，省 flash 且时序可控。
 * 两个 UART 外设本身都由 SYSCFG_DL_init() 初始化，这里只封装发送与分发。
 *
 * 引脚为什么是 PB2/PB3 而不是复用调试口：见 car.syscfg 里 ESP_UART 那段注释 + 载板 §10.1。
 * 双发的时间代价与何时该关掉一路：见 config.h §9 CFG_UART_SINKS_BOOT。
 */

#define UART_SINK_DAP   0x1u    /* 有线：UART0 -> DAP VCOM */
#define UART_SINK_ESP   0x2u    /* 无线：UART3 -> ESP-01S  */
#define UART_SINK_BOTH  (UART_SINK_DAP | UART_SINK_ESP)

void uart_dbg_putc(char c);
void uart_dbg_puts(const char *s);   /* 遇 '\n' 自动补 '\r', 串口助手换行整齐 */
void uart_dbg_put_int(int32_t v);    /* 带符号十进制 */

/* 运行时切换输出去向(位掩码, 见上面 UART_SINK_*)。
 * 🔁 **2026-07-31 改：mask=0 现在合法**（原注释"mask=0 会被忽略，因为等于把自己变瞎"已作废）。
 *   理由 = 官方答疑 Q62 明文"测试期间仅允许图传工作" ⇒ 必须能一键关掉无线遥测，挡掉 0 就是合规缺口。
 *   而"变瞎"这个顾虑不成立: ① RX 与 sink 无关，关了照样能发 `l3` 恢复;
 *   ② LCD RUN 页(`u2`)是独立观测通道。⚠ 调用方须**先打回执再关**，否则没有口能收到确认。 */
void     uart_dbg_set_sinks(uint32_t mask);
uint32_t uart_dbg_get_sinks(void);

#endif /* UART_DBG_H */
