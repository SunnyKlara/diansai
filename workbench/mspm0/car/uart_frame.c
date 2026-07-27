/*
 * uart_frame.c - 外部智能模块 -> MCU 的串口帧解析实现（视觉坐标链）
 *
 * 帧格式、字段含义、两条安全默认全在 uart_frame.h；这里只写实现上的判断。
 *
 * PC 验证:
 *   cd pc_test && gcc -O2 -Wall -I.. -o test_uart_frame test_uart_frame.c ../uart_frame.c
 *   ./test_uart_frame
 *
 * 状态: 2026-07-27 补齐实现（此前 uart_frame.h 是**孤儿头文件**：没有 .c、不在 makefile、
 *       自称的 pc_test 也不存在 ⇒ "视觉串口链已就绪"是假象）。现在 PC 单测已过 + 已编进固件，
 *       但 **真机零验证**：相机模块还没到手，MCU 侧也还没配第三路 UART（见文件末的接入说明）。
 *
 * 不依赖 <string.h>/<stdlib.h>：全部手写，省 flash 且能在任何环境下 PC 单测。
 */
#include "uart_frame.h"

/* '0'-'9' 'A'-'F' 'a'-'f' -> 0..15；非法返回 -1 */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;   /* 小写也收: 发端用 %02x 是常见写法 */
    return -1;
}

uint8_t uf_checksum(const char *body, uint16_t n)
{
    uint8_t x = 0;
    uint16_t i;
    for (i = 0; i < n; i++) x ^= (uint8_t)body[i];
    return x;
}

void uf_init(uf_parser_t *p)
{
    p->last.id = UF_ID_NONE;
    p->last.cx = p->last.cy = p->last.area = 0;
    p->last.stamp_ms = 0;
    p->have_frame = 0;
    p->n_ok = p->n_bad_csum = p->n_bad_form = p->n_overflow = 0;
    p->len = 0;
    p->in_frame = 0;
}

/* 严格整数解析：允许前导 '-'，其余必须全是数字，空字段/单个 '-' 都算非法。
 * ⚠ 严格是有理由的：宽松解析(如 atoi)会把 "12x" 读成 12、把 "" 读成 0 —— 一个坏帧于是变成
 *   一个"看起来合法的坐标"，车会朝着不存在的目标开过去。宁可整帧丢掉。 */
static int parse_i32(const char *s, uint16_t n, int32_t *out)
{
    uint16_t i = 0;
    int neg = 0;
    int32_t v = 0;
    if (n == 0) return 0;
    if (s[0] == '-') { neg = 1; i = 1; if (n == 1) return 0; }
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (int32_t)(s[i] - '0');
    }
    *out = neg ? -v : v;
    return 1;
}

/* buf[0..len) = '$' 之后、'\n' 之前的全部字符。解析成功返回 1。 */
static int uf_parse(uf_parser_t *p, uint32_t now_ms)
{
    uint16_t n = p->len, i;
    int star = -1;

    for (i = 0; i < n; i++) { if (p->buf[i] == '*') { star = (int)i; break; } }
    if (star < 0)                 { p->n_bad_form++; return 0; }   /* 没有校验分隔符 */
    if (n - (uint16_t)star != 3)  { p->n_bad_form++; return 0; }   /* '*' 后必须正好 2 位 HEX */

    {
        int h1 = hexval(p->buf[star + 1]), h0 = hexval(p->buf[star + 2]);
        if (h1 < 0 || h0 < 0)     { p->n_bad_form++; return 0; }
        /* 校验和不匹配单独计数(n_bad_csum): 它指向**线路噪声或发端算错**,
         * 与"格式写错"是两种完全不同的故障, 现场排障方向也不同。 */
        if (uf_checksum(p->buf, (uint16_t)star) != (uint8_t)((h1 << 4) | h0)) {
            p->n_bad_csum++; return 0;
        }
    }

    /* 拆 5 个逗号字段: V,id,cx,cy,area */
    {
        uint16_t start[5], flen[5];
        int nf = 0;
        uint16_t seg = 0;
        for (i = 0; i <= (uint16_t)star; i++) {
            if (i == (uint16_t)star || p->buf[i] == ',') {
                if (nf >= 5) { p->n_bad_form++; return 0; }        /* 字段太多 */
                start[nf] = seg; flen[nf] = (uint16_t)(i - seg);
                nf++; seg = (uint16_t)(i + 1);
            }
        }
        if (nf != 5)                              { p->n_bad_form++; return 0; }
        if (flen[0] != 1 || p->buf[start[0]] != 'V') { p->n_bad_form++; return 0; }  /* 标识必须是 V */

        {
            int32_t id, cx, cy, area;
            if (!parse_i32(&p->buf[start[1]], flen[1], &id)   ||
                !parse_i32(&p->buf[start[2]], flen[2], &cx)   ||
                !parse_i32(&p->buf[start[3]], flen[3], &cy)   ||
                !parse_i32(&p->buf[start[4]], flen[4], &area)) { p->n_bad_form++; return 0; }

            /* ★ id=UF_ID_NONE 的帧也是**有效帧**, 一样更新时间戳。
             * 这正是本设计的要点: "模块活着但没看到目标"(NO_TARGET) 与 "模块掉线了"(STALE)
             * 是两件事, 上层的处置完全不同(前者继续搜索, 后者要去查线/查模块)。
             * 若这种帧不更新时间戳, 一个视野里没目标的正常相机会被误判成掉线。 */
            p->last.id = id;
            p->last.cx = cx;
            p->last.cy = cy;
            p->last.area = area;
            p->last.stamp_ms = now_ms;
            p->have_frame = 1;
            p->n_ok++;
            return 1;
        }
    }
}

int uf_push(uf_parser_t *p, char c, uint32_t now_ms)
{
    if (c == '$') {
        /* 重新同步: 帧中途又见 '$' ⇒ 上一帧被截断了(丢字节/发端复位)。
         * 记一次格式错再重开, **不要**试图把两个半截帧拼起来 —— 拼出来的坐标是合法数值、
         * 却指向一个不存在的目标, 比丢帧危险得多。 */
        if (p->in_frame && p->len > 0) p->n_bad_form++;
        p->in_frame = 1;
        p->len = 0;
        return 0;
    }
    if (!p->in_frame) return 0;      /* 帧外字节直接丢(遥测/日志混在同一条线上时全靠这一条) */
    if (c == '\r') return 0;         /* 吃掉 CR, 所以 "\r\n" 与 "\n" 都能收 */
    if (c == '\n') {
        int ok = 0;
        if (p->len > 0) ok = uf_parse(p, now_ms);
        p->in_frame = 0;
        p->len = 0;
        return ok;
    }
    if (p->len >= UF_BUF_LEN) {
        /* 超长还没等到 '\n': 波特率不匹配的典型症状(字节全是垃圾、永远等不到换行)。
         * 单独计数, 因为它的修法是"去核对波特率", 不是"去改帧格式"。 */
        p->n_overflow++;
        p->in_frame = 0;
        p->len = 0;
        return 0;
    }
    p->buf[p->len++] = c;
    return 0;
}

int uf_fresh(const uf_parser_t *p, uint32_t now_ms)
{
    if (!p->have_frame) return 0;
    /* 无符号相减 => 时钟回绕安全(SysTick 毫秒计数会在 ~49 天后回绕, 赛场用不到但零成本) */
    return ((uint32_t)(now_ms - p->last.stamp_ms) <= (uint32_t)UF_STALE_MS) ? 1 : 0;
}

uf_status_t uf_status(const uf_parser_t *p, uint32_t now_ms)
{
    if (!p->have_frame)            return UF_NO_DATA;
    if (!uf_fresh(p, now_ms))      return UF_STALE;
    if (p->last.id == UF_ID_NONE)  return UF_NO_TARGET;
    return UF_OK;
}

int uf_get(const uf_parser_t *p, uint32_t now_ms, uf_target_t *out)
{
    if (uf_status(p, now_ms) != UF_OK) return 0;   /* 过期数据绝不冒充有效 */
    if (out) *out = p->last;
    return 1;
}

/* ── 接进固件还差什么（相机到手后约 15 分钟）──────────────────────────────
 * 现在这份解析器**已编进固件并已接到现有两个串口上**（car.c 的 feed_cmd_stream 会把以 '$'
 * 开头的行喂给 uf_push），所以：
 *   · 相机可以先接在**有线调试口的 USB-TTL** 上试，或者
 *   · 干脆**由 PC 直接发 `$V,...` 帧**来测整条视觉伺服链（m10），一台相机都不需要。
 * 真要让相机独占一路 UART，则需要：
 *   ① car.syscfg 加一个 UART 实例（载板给 ESP#1 预留的 PB15/PB16 = UART2，**复用号待核**）；
 *   ② car.c 的 poll_uart 里多一条 while(receiveDataCheck(...)) 把字节喂给 uf_push。
 * 别忘了本仓库那条坑：**新模块必须同时进 makefile 和 syscfg**，否则就是又一个孤儿。
 */
