/*
 * linesens.c - 八路循迹 YB-MUX04-1.0 驱动 · **IO 方式（X1..X8 直读电平）**
 *
 * ============ 为什么从串口改成 GPIO（2026-07-29 换线，有实测支撑）============
 * 串口方式**物理层是通的**（真机 `rx=386308` 字节、波特率 115200 无误），但解析永远失败：
 *     rx=386308  frames=1862  D=0  bad=6200  ovf=20  lost=0
 * 关键是 `lost=0` 而 `bad` 巨大 —— 环形缓冲一个字节都没追尾，ASCII 里却是
 *     `1,x4x5:1,,x8:#$D,,x2:1,x4x6:1,...`
 * 这种 `x4:1,` 中间被吞掉的残帧 ⇒ **字节丢在硬件 RX FIFO，还没进软件缓冲**。
 * 算一下就知道纯轮询无解：115200 下一字节 87µs 到，MSPM0 的 RX FIFO 只 4 字节
 * ⇒ 必须每 ~350µs 轮一次；而主循环末尾有 `delay_ms(1)`、LCD 重绘一次还要 ~100ms。
 * 上 RX 中断也没救活（isr=0：IMASK/NVIC 实测都已置位，RIS 的 RXINT 却从不置起，
 * 同时 OVRERR 已置 ⇒ 还有一层没查清）。**不再往下挖：换掉整条链路，问题类型直接消失。**
 *
 * IO 方式为什么是正解（不是妥协）：
 *   · 厂家文档明写三种协议**并列**：`直接IO电平读取` / `USART` / `I2C` ——
 *     X1..X8 是一直活着的数字输出，**不需要切模式、不需要发使能命令**。
 *   · 没有帧、没有 FIFO、没有波特率、没有中断时序 ⇒ **轮询多慢都不丢**，
 *     一个电平永远在那儿等着被读。这一步把"整类时序问题"从工程里删掉了。
 *   · 顺带把 `PB4/PB5` 还给 K230 相机 UART1（载板 §10.1 原设计，2026-H 正好要用）。
 *   · 代价：占 8 个 GPIO（取自载板 §10.12 空闲清单）+ 拿不到模拟值（IO 方式只有数字量）。
 *     数字量对循迹够用 —— `line.c` 的加权质心用 0/1 就能出横向偏差。
 *
 * ============ 为什么保留 `linesens_*` 这套函数名 ============
 * car.c / m11 / RUN 页 / `L` 命令全都在调它们，且 `g_lf` 是唯一的数据出口
 * （car.c 只通过 `lf_get_digital(&g_lf, ...)` 取数）。**保持 API 不变 ⇒ car.c 零改动**，
 * 赛前把改动面压到最小。名字里的"串口"含义已过时，语义见本注释。
 * 波特率相关的几个函数退化为空实现（`B`/`X`/`T`/`Q` 命令随之失效，它们是串口排障专用）。
 *
 * 状态: 2026-07-29 改写。**编译级 + 真机待验证**（引脚见 car.syscfg 的 GPIO_LINE）。
 */
#include "linesens.h"
#include "lineframe.h"
#include "uart_dbg.h"
#include "ti_msp_dl_config.h"

#if CFG_LINE_UART_EN

lf_t g_lf;                     /* 非 static: car.c 要读它出横向偏差 */

static uint32_t g_reads;       /* 成功读取次数（对外沿用 rx_total 的位置） */
static uint32_t g_changes;     /* 8 路位型发生变化的次数（对外沿用 frames 的位置） */
static uint32_t g_last_mask;   /* 上次的位型，用于统计变化 */
static int      g_have_last;

/* X1..X8 → 引脚表。**顺序就是模块丝印 X1..X8 的顺序**，接线错位会直接表现为
 * "偏差符号反/质心偏一格"，所以这张表和实际插线必须逐一对上（dump 会把两者一起打出来）。 */
typedef struct { GPIO_Regs *port; uint32_t pin; const char *name; } line_pin_t;

static const line_pin_t LINE_PIN[LF_CH] = {
    { GPIO_LINE_X1_PORT, GPIO_LINE_X1_PIN, "X1/PB17" },
    { GPIO_LINE_X2_PORT, GPIO_LINE_X2_PIN, "X2/PB27" },
    { GPIO_LINE_X3_PORT, GPIO_LINE_X3_PIN, "X3/PA17" },
    { GPIO_LINE_X4_PORT, GPIO_LINE_X4_PIN, "X4/PA25" },
    { GPIO_LINE_X5_PORT, GPIO_LINE_X5_PIN, "X5/PA21" },
    { GPIO_LINE_X6_PORT, GPIO_LINE_X6_PIN, "X6/PA23" },
    { GPIO_LINE_X7_PORT, GPIO_LINE_X7_PIN, "X7/PB18" },
    { GPIO_LINE_X8_PORT, GPIO_LINE_X8_PIN, "X8/PB23" },
};

void linesens_init(void)
{
    lf_init(&g_lf);
    g_reads = 0; g_changes = 0; g_last_mask = 0; g_have_last = 0;
    /* 引脚方向/上拉由 SysConfig 的 GPIO_LINE 配好（INPUT + PULL_UP），这里无事可做。
     * **故意不发任何命令、不碰 UART** —— IO 方式下模块不需要被使能。 */
}

int linesens_poll(uint32_t now_ms)
{
    uint32_t mask = 0;
    int i;
    /* 直接读电平。**极性**: 模块协议规定 `0 = 在黑线上`（与 `lf_t.dig` 约定一致，
     * 见 lineframe.h：`dig[] 0/1，0 = 在黑线上`）⇒ 原样填入，不取反。 */
    for (i = 0; i < LF_CH; i++) {
        int hi = (DL_GPIO_readPins(LINE_PIN[i].port, LINE_PIN[i].pin) != 0);
        g_lf.dig[i] = hi ? 1 : 0;
        if (hi) mask |= (1u << i);
    }
    /* 让 `lf_get_digital()` 认为数据是新鲜的 —— 它只看 `t_dig_ms` 与 `n_dig`。
     * 这是"GPIO 驱动冒充帧解析器"的唯一接口点，除此之外 lf_t 的字段都不用动。 */
    g_lf.t_dig_ms = now_ms;
    g_lf.n_dig++;
    g_reads++;
    if (!g_have_last || mask != g_last_mask) { g_changes++; g_last_mask = mask; g_have_last = 1; }
    return LF_CH;
}

void linesens_dump(void)
{
    int i, on = 0;
    uart_dbg_puts("\n[line] mode=GPIO(IO方式) reads=");  uart_dbg_put_int((int32_t)g_reads);
    uart_dbg_puts(" changes=");                          uart_dbg_put_int((int32_t)g_changes);
    uart_dbg_puts(" D=");                                uart_dbg_put_int((int32_t)g_lf.n_dig);
    /* 当前电平位型 —— 排障时最想看的一行。 */
    uart_dbg_puts("\n[line] dig(0=在黑线上) x1..x8:");
    for (i = 0; i < LF_CH; i++) {
        uart_dbg_puts(" ");
        uart_dbg_put_int(g_lf.dig[i]);
        if (g_lf.dig[i] == 0) on++;
    }
    uart_dbg_puts("  在线路数=");  uart_dbg_put_int((int32_t)on);
    /* 引脚映射一起打：接线错位是这条链路唯一还可能出的错，把"软件以为的"摊开给人对。 */
    uart_dbg_puts("\n[line] pins:");
    for (i = 0; i < LF_CH; i++) { uart_dbg_puts(" "); uart_dbg_puts(LINE_PIN[i].name); }
    if (on == 0) {
        /* 全 1（全白）既可能是真的悬空在白底上, 也可能是 8 根线一根没接（上拉把脚拉高）。
         * 这两种在电平上**完全一样**, 所以判据只能靠"遮一路看它变不变"。 */
        uart_dbg_puts("\n[line] 全为1 => 要么真在白底上, 要么线没接(内部上拉也会读到1)。"
                      "判据: 用手指/黑纸遮住某一路探头再发 L, 对应位应变 0");
    }
    if (on == LF_CH) uart_dbg_puts("\n[line] 全为0 => 全部在黑线上(可能压着启停线), 或 8 根线短地");
    uart_dbg_puts("\n");
}

/* ==== 以下是串口时代的接口, IO 方式下退化为空实现 ====
 * 保留函数体是为了让 car.c 的 `B`/`X`/`T`/`Q` 命令仍能编译通过（它们是串口排障专用,
 * 现在没有意义）。**故意不删声明**: 赛前删跨文件符号的收益远小于风险。 */
void     linesens_set_baud(uint32_t baud) { (void)baud; }
uint32_t linesens_get_baud(void)          { return 0; }      /* 0 = "没有串口这回事" */
uint32_t linesens_rx_total(void)          { return g_reads; }
uint32_t linesens_frames(void)            { return g_changes; }
void     linesens_clear(void)             { g_reads = 0; g_changes = 0; g_have_last = 0; lf_init(&g_lf); }
void     linesens_tx(uint8_t b)           { (void)b; }
int      linesens_selftest_loopback(void) { return -1; }     /* -1 = 不适用 */
void     linesens_send_enable(void)       { }

#else  /* !CFG_LINE_UART_EN —— 没编入时给出空实现, 免得调用点到处 #if */
void     linesens_init(void)                   { }
void     linesens_set_baud(uint32_t baud)      { (void)baud; }
uint32_t linesens_get_baud(void)               { return 0; }
int      linesens_poll(uint32_t now_ms)        { (void)now_ms; return 0; }
void     linesens_dump(void)                   { }
uint32_t linesens_rx_total(void)               { return 0; }
uint32_t linesens_frames(void)                 { return 0; }
void     linesens_clear(void)                  { }
void     linesens_tx(uint8_t b)                { (void)b; }
int      linesens_selftest_loopback(void)      { return -1; }
void     linesens_send_enable(void)            { }
#endif
