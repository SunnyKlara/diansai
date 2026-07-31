/*
 * uart_dbg.c - 轻量调试输出，同一份文本可同时发到有线口与无线口(阻塞逐字节)
 *   DAP: UART0, TX=PA10 / RX=PA11 (板载 DAP 的 VCOM)
 *   ESP: UART3, TX=PB2  / RX=PB3  (车载 ESP-01S 无线桥, CFG_ESP_UART_EN=1 时编入)
 * 只做 TX 打印(log): 逐字节阻塞发, 不依赖 printf/newlib, 省 flash 且时序可控。
 * 外设初始化在 SYSCFG_DL_init() 里 (syscfg 生成 DBG_UART_INST / ESP_UART_INST)。
 *
 * 为什么做成"分发"而不是在 car.c 里到处加第二次调用:
 *   car.c 有近百处 uart_dbg_puts/put_int, 逐处加等于改一百个地方且必然漏;
 *   在这里 tee 一次, 上层零改动 —— 遥测/报错/boot banner 自动同时上无线。
 */
#include "ti_msp_dl_config.h"
#include "uart_dbg.h"
#include "config.h"

#if CFG_ESP_UART_EN
static uint32_t g_sinks = (uint32_t)CFG_UART_SINKS_BOOT & UART_SINK_BOTH;
#else
static uint32_t g_sinks = UART_SINK_DAP;   /* 没编 UART3 时只可能走有线 */
#endif

/* 唯一的出口: 所有打印最终都从这里出去(加第三个口也只改这一处) */
static void tx_byte(uint8_t b)
{
    if (g_sinks & UART_SINK_DAP)
        DL_UART_transmitDataBlocking(DBG_UART_INST, b);
#if CFG_ESP_UART_EN
    if (g_sinks & UART_SINK_ESP)
        DL_UART_transmitDataBlocking(ESP_UART_INST, b);
#endif
}

void uart_dbg_set_sinks(uint32_t mask)
{
    mask &= UART_SINK_BOTH;
#if !CFG_ESP_UART_EN
    mask &= UART_SINK_DAP;
#endif
    /* 🔁 2026-07-31: **mask=0 现在是合法的**（原先被这里和 car.c 两处一起挡掉）。
     * 为什么必须允许: 官方答疑 Q62 明文"测试期间仅允许图传工作" ⇒ 正式测试要能一键关掉无线遥测。
     * 挡掉 0 的原代码使这件事**做不到**, 那不是保护、是合规缺口。
     * 为什么现在敢关: ① 命令通道(RX)与 sink(TX)无关 ⇒ 关了照样能发 `l3` 恢复;
     *   ② LCD 的 RUN 页(`u2`, 走时/状态/里程/球位)是独立的观测通道, 关掉串口不等于变瞎。
     * ⚠ 调用方有义务**在关闭之前**把回执打出来 —— 关完再打就没有任何口能收到了(car.c 已如此处理)。*/
    g_sinks = mask;
}

uint32_t uart_dbg_get_sinks(void) { return g_sinks; }

void uart_dbg_putc(char c)
{
    tx_byte((uint8_t)c);
}

void uart_dbg_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            tx_byte((uint8_t)'\r');
        tx_byte((uint8_t)*s++);
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
