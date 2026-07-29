/*
 * disp_run.c —— RUN 页纯排版（设计判断见 disp_run.h 文件头）
 *
 * 不用 snprintf: 算法层不依赖 newlib, 且 MCU 上 printf 家族又大又慢(会把 flash 撑掉几 KB,
 * 还可能在中断上下文里出岔子)。全部手写定长格式化。
 */
#include "disp_run.h"

/* ── 小工具: 往 buf 追加, 带边界保护 ──────────────────────────────── */
static int put_c(char *b, int i, int cap, char c)
{
    if (i < cap - 1) b[i++] = c;
    return i;
}

static int put_s(char *b, int i, int cap, const char *s)
{
    while (*s) i = put_c(b, i, cap, *s++);
    return i;
}

/* 无符号十进制, width>0 时左侧补 pad 到指定宽度 */
static int put_u(char *b, int i, int cap, uint32_t v, int width, char pad)
{
    char tmp[12];
    int n = 0;
    if (v == 0u) tmp[n++] = '0';
    while (v > 0u && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (width > n) { i = put_c(b, i, cap, pad); width--; }
    while (n > 0) i = put_c(b, i, cap, tmp[--n]);
    return i;
}

/* 右侧补空格到固定宽度并收尾 —— 覆盖式绘制靠它擦掉上一帧的残字(见 .h 注释 ②) */
static void pad_end(char *b, int i, int cap)
{
    while (i < cap - 1) b[i++] = ' ';
    b[cap - 1] = '\0';
}

/* 定点一位小数: v_x10 = 数值×10。带符号。 */
static int put_f1(char *b, int i, int cap, int32_t v_x10, int force_sign)
{
    uint32_t a;
    if (v_x10 < 0)      { i = put_c(b, i, cap, '-'); a = (uint32_t)(-v_x10); }
    else                { if (force_sign) i = put_c(b, i, cap, '+'); a = (uint32_t)v_x10; }
    i = put_u(b, i, cap, a / 10u, 0, ' ');
    i = put_c(b, i, cap, '.');
    i = put_u(b, i, cap, a % 10u, 1, '0');
    return i;
}

static int32_t round_x10(float v)
{
    return (int32_t)((v < 0.0f) ? (v * 10.0f - 0.5f) : (v * 10.0f + 0.5f));
}

/* ── 各行 ───────────────────────────────────────────────────────── */

/* L0 走时: <60s -> "12.3" ; >=60s -> "1:02.3"（与 task_fmt_time 同格式，刻意重写一份：
 * 显示层不该依赖任务层，否则 disp_run 就不能独立单测了） */
static void line_time(char *b, int cap, uint32_t ms)
{
    uint32_t ds = ms / 100u;
    uint32_t d = ds % 10u, s = (ds / 10u) % 60u, m = (ds / 10u) / 60u;
    int i = 0;
    if (m > 0u) {
        if (m > 99u) m = 99u;
        i = put_u(b, i, cap, m, 0, ' ');
        i = put_c(b, i, cap, ':');
        i = put_u(b, i, cap, s, 2, '0');
    } else {
        i = put_u(b, i, cap, s, 0, ' ');
    }
    i = put_c(b, i, cap, '.');
    i = put_u(b, i, cap, d, 1, '0');
    pad_end(b, i, cap);
}

static void line_state(char *b, int cap, int st)
{
    static const char *N[] = { "READY", "RUN", "BRAKE", "DONE", "ABORT" };
    int i = put_s(b, 0, cap, (st >= 0 && st <= 4) ? N[st] : "???");
    pad_end(b, i, cap);
}

static void line_fail(char *b, int cap, int st, int fail)
{
    int i = 0;
    /* 只有真的异常停才占这一行 —— 平时留空, 别让屏上常驻一个 "NONE" 让人误会 */
    if (st == 4) {
        static const char *N[] = { "STOP", "TIMEOUT", "LOST", "MANUAL" };
        i = put_s(b, 0, cap, (fail >= 0 && fail <= 3) ? N[fail] : "FAIL?");
    }
    pad_end(b, i, cap);
}

/* 里程用**米 + 两位小数**显示: 一圈 6.14m; 用 mm 会是 5 位数, 在 240px 小屏上白占宽度。
 * 先把 mm 四舍五入成整数厘米再手插小数点 —— 避免 float 除法在 6141.6 这种值上抖出 6.13/6.15。 */
static void line_dist(char *b, int cap, float mm)
{
    int32_t cm_s = (int32_t)((mm < 0.0f) ? (mm / 10.0f - 0.5f) : (mm / 10.0f + 0.5f));
    uint32_t cm  = (uint32_t)((cm_s < 0) ? -cm_s : cm_s);
    int i = 0;
    if (cm_s < 0) i = put_c(b, i, cap, '-');
    i = put_u(b, i, cap, cm / 100u, 0, ' ');    /* 米 */
    i = put_c(b, i, cap, '.');
    i = put_u(b, i, cap, cm % 100u, 2, '0');    /* 厘米 */
    i = put_c(b, i, cap, 'm');
    pad_end(b, i, cap);
}

static void line_ball(char *b, int cap, float mm)
{
    int i;
    if (mm <= DISP_RUN_NO_BALL + 1.0f) {           /* 无效读数 -> 明确显示没有, 不显示 0.0 */
        i = put_s(b, 0, cap, "ball --");
    } else {
        i = put_s(b, 0, cap, "b");
        i = put_f1(b, i, cap, round_x10(mm), 1);   /* 带正负号: 球在 O 的哪一侧一眼可见 */
    }
    pad_end(b, i, cap);
}

static void line_meta(char *b, int cap, uint32_t n_runs, uint32_t n_lost)
{
    int i = put_c(b, 0, cap, '#');
    i = put_u(b, i, cap, n_runs, 0, ' ');
    i = put_s(b, i, cap, " L");
    i = put_u(b, i, cap, n_lost, 0, ' ');
    pad_end(b, i, cap);
}

void disp_run_build(const disp_run_in_t *in, disp_run_txt_t *cur)
{
    if (!in || !cur) return;
    line_time (cur->line[0], DISP_RUN_LEN, in->elapsed_ms);
    line_state(cur->line[1], DISP_RUN_LEN, in->state);
    line_fail (cur->line[2], DISP_RUN_LEN, in->state, in->fail);
    line_dist (cur->line[3], DISP_RUN_LEN, in->dist_mm);
    line_ball (cur->line[4], DISP_RUN_LEN, in->ball_mm);
    line_meta (cur->line[5], DISP_RUN_LEN, in->n_runs, in->n_lost);
}

uint32_t disp_run_diff(const disp_run_txt_t *prev, const disp_run_txt_t *cur)
{
    uint32_t m = 0u;
    int i, j;
    if (!cur) return 0u;
    if (!prev) return (1u << DISP_RUN_LINES) - 1u;      /* 首帧全画 */
    for (i = 0; i < DISP_RUN_LINES; i++) {
        for (j = 0; j < DISP_RUN_LEN; j++) {
            if (prev->line[i][j] != cur->line[i][j]) { m |= (1u << i); break; }
        }
    }
    return m;
}
