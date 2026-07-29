/*
 * lineframe.c - 八路巡线模块串口帧解析（实现）
 * 设计理由与安全默认全部写在 lineframe.h，这里只放实现。
 * 纯算法层：不含任何 HAL 调用，可 PC 编译（pc_test/test_lineframe.c）。
 * 状态: 2026-07-29 新建。**PC 单测已过；真机未验**
 */
#include "lineframe.h"

void lf_init(lf_t *L)
{
    for (int i = 0; i < LF_CH; i++) { L->dig[i] = 1; L->ana[i] = -1; }
    L->t_dig_ms = 0; L->t_ana_ms = 0;
    L->n_dig = 0; L->n_ana = 0; L->n_bad = 0; L->n_ovf = 0;
    L->len = 0; L->in_frame = 0;
}

/* 解析 "x<idx>:<num>" 一组。p 指向 'x'；成功返回下一个待解析位置，失败返回 NULL。
 * want_idx 是**期望的**通道号(1..8) —— 强制顺序，防止"字段串位"被当成合法帧。 */
static const char *parse_pair(const char *p, const char *end, int want_idx, long *val)
{
    if (p >= end || *p != 'x') return 0;
    p++;
    if (p >= end || *p < '0' || *p > '9') return 0;
    int idx = 0;
    while (p < end && *p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; }
    if (idx != want_idx) return 0;                 /* 顺序必须严格 x1..x8 */
    if (p >= end || *p != ':') return 0;
    p++;
    if (p >= end || *p < '0' || *p > '9') return 0; /* 必须有数字，且协议里无负值 */
    long v = 0;
    int nd = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0'); p++; nd++;
        if (nd > 5) return 0;                       /* 防畸形长数字 */
    }
    *val = v;
    return p;
}

/* 解析一整帧（buf 内容不含 '$' 与 '#'）。成功返回 'D'/'A'，失败返回 0。 */
static char parse_body(lf_t *L, uint32_t now_ms)
{
    const char *p   = L->buf;
    const char *end = L->buf + L->len;
    if (p >= end) return 0;
    char kind = *p++;
    if (kind != 'D' && kind != 'A') return 0;
    if (p >= end || *p != ',') return 0;
    p++;
    long v[LF_CH];
    for (int i = 0; i < LF_CH; i++) {
        p = parse_pair(p, end, i + 1, &v[i]);
        if (!p) return 0;
        if (i < LF_CH - 1) {                        /* 前 7 组后面必须是逗号 */
            if (p >= end || *p != ',') return 0;
            p++;
        }
    }
    if (p != end) return 0;                         /* 末尾不许有多余字符 */
    /* 值域校验 —— 没有校验和时这是唯一能挡住误码的东西 */
    if (kind == 'D') {
        for (int i = 0; i < LF_CH; i++) if (v[i] != 0 && v[i] != 1) return 0;
        for (int i = 0; i < LF_CH; i++) L->dig[i] = (int)v[i];
        L->t_dig_ms = now_ms; L->n_dig++;
    } else {
        for (int i = 0; i < LF_CH; i++) if (v[i] < 0 || v[i] > LF_ANA_MAX) return 0;
        for (int i = 0; i < LF_CH; i++) L->ana[i] = (int)v[i];
        L->t_ana_ms = now_ms; L->n_ana++;
    }
    return kind;
}

int lf_push(lf_t *L, char c, uint32_t now_ms)
{
    if (c == '$') {                    /* 帧首：无条件重新开始（半截帧就地丢掉） */
        L->in_frame = 1; L->len = 0;
        return 0;
    }
    if (!L->in_frame) return 0;        /* 帧外字节忽略（模块上电噪声、别的模块串台） */
    if (c == '#') {
        L->in_frame = 0;
        char k = parse_body(L, now_ms);
        if (!k) { L->n_bad++; return 0; }
        return 1;
    }
    if (c == '\r' || c == '\n') return 0;   /* 容忍行尾 */
    if (L->len >= LF_BUF) {           /* 溢出：说明没收到 '#'，链路在丢字节 */
        L->in_frame = 0; L->n_ovf++; L->len = 0;
        return 0;
    }
    L->buf[L->len++] = c;
    return 0;
}

static int fresh(uint32_t t, uint32_t now, uint32_t max_age)
{
    if (t == 0) return 0;                          /* 从未收到 */
    return (uint32_t)(now - t) <= max_age;
}

int lf_get_digital(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms, int out[LF_CH])
{
    if (!fresh(L->t_dig_ms, now_ms, max_age_ms)) return 0;
    for (int i = 0; i < LF_CH; i++) out[i] = L->dig[i];
    return 1;
}

int lf_get_analog(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms, int out[LF_CH])
{
    if (!fresh(L->t_ana_ms, now_ms, max_age_ms)) return 0;
    for (int i = 0; i < LF_CH; i++) out[i] = L->ana[i];
    return 1;
}

lf_status_t lf_status(const lf_t *L, uint32_t now_ms, uint32_t max_age_ms)
{
    if (L->n_dig == 0 && L->n_ana == 0) return LF_NO_DATA;
    uint32_t t = (L->t_dig_ms > L->t_ana_ms) ? L->t_dig_ms : L->t_ana_ms;
    return fresh(t, now_ms, max_age_ms) ? LF_OK : LF_STALE;
}
