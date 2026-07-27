/*
 * line.c - 光电循迹算法层实现（阶梯 5）。纯算法，不依赖 HAL。
 *
 * PC 验证:
 *   cd pc_test && gcc -O2 -Wall -I.. -o test_line test_line.c ../line.c && ./test_line
 *
 * 设计取舍（为什么标定是一等公民、为什么对比度不足要报错、符号约定）全在 line.h。
 * 状态: 2026-07-27 新建。PC 单测已过；**真机零验证，硬件尚未接线**。
 */
#include "line.h"
#include "config.h"

static int clampi_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int absi_(int v) { return v < 0 ? -v : v; }

void line_init(line_t *L, int n, const float *pos)
{
    int i;
    if (n < 1) n = 1;
    if (n > LINE_MAX_CH) n = LINE_MAX_CH;
    L->n = n;
    for (i = 0; i < LINE_MAX_CH; i++) {
        /* 默认等间距铺开、**左为正**、中线为 0。例: n=5, 间距 12mm -> +24,+12,0,-12,-24
         * (即 raw[0] 是最左边那个探头 —— 接线时别插反, 插反了循迹会朝反方向跑飞) */
        L->pos[i]   = (i < n) ? ((float)(n - 1) / 2.0f - (float)i) * (float)CFG_LINE_PITCH_MM : 0.0f;
        L->ref_w[i] = 0;
        L->ref_b[i] = 0;
        L->norm[i]  = 0;
    }
    if (pos) for (i = 0; i < n; i++) L->pos[i] = pos[i];

    L->have_w = L->have_b = 0;
    L->on_thresh    = CFG_LINE_ON_THRESH;
    L->min_contrast = CFG_LINE_MIN_CONTRAST;
    L->kp           = CFG_KP_LINE;
    L->kd           = CFG_KD_LINE;
    L->w_max        = CFG_LINE_W_MAX;
    L->search_w     = CFG_LINE_SEARCH_W;
    L->on_mask  = 0;
    L->err      = 0.0f;
    L->last_err = 0.0f;
    L->last_dir = 1.0f;      /* 任取一个初值; 第一次压到线上就会被真实方向覆盖 */
    L->lost_ms  = 0;
}

void line_cal_white(line_t *L, const int *raw)
{
    int i;
    for (i = 0; i < L->n; i++) L->ref_w[i] = raw[i];
    L->have_w = 1;
}

void line_cal_black(line_t *L, const int *raw)
{
    int i;
    for (i = 0; i < L->n; i++) L->ref_b[i] = raw[i];
    L->have_b = 1;
}

int line_bad_channel(const line_t *L)
{
    int i;
    if (!L->have_w || !L->have_b) return 0;      /* 还没标定, 谈不上哪个通道坏 */
    for (i = 0; i < L->n; i++)
        if (absi_(L->ref_b[i] - L->ref_w[i]) < L->min_contrast) return i;
    return -1;
}

int line_calibrated(const line_t *L)
{
    if (!L->have_w || !L->have_b) return 0;
    return (line_bad_channel(L) < 0) ? 1 : 0;
}

int line_normalize(const line_t *L, int ch, int raw)
{
    /* 白=0, 黑=1000。**用差值做分母 ⇒ 自动兼容"黑读数更低"的传感器**(反射式 vs 透射式、
     * 有的模块带反相输出) —— 不需要一个"极性"配置项, 少一个能配错的东西。 */
    int span = L->ref_b[ch] - L->ref_w[ch];
    if (span == 0) return 0;
    return clampi_((raw - L->ref_w[ch]) * 1000 / span, 0, 1000);
}

line_state_t line_step(line_t *L, const int *raw, float dt_s, float *err_out, int *w_out)
{
    int i, on_cnt = 0;
    long wsum = 0;               /* 权重和(归一化值之和) */
    float num = 0.0f;            /* Σ pos_i * norm_i */
    float err;
    int wi = 0;

    if (err_out) *err_out = 0.0f;
    if (w_out)   *w_out = 0;

    /* 反馈健康门: 没标定 / 有通道对比度不足 -> 明确拒绝, 不输出任何转向。
     * 这一条是"先证反馈可信再碰控制器"的落地: 归一化在对比度不足时输出的是纯噪声,
     * 让噪声去开车比不动更糟(而且现象会像"PID 怎么都调不好")。 */
    if (!line_calibrated(L)) {
        L->on_mask = 0;
        return LINE_NOCAL;
    }

    L->on_mask = 0;
    for (i = 0; i < L->n; i++) {
        int nv = line_normalize(L, i, raw[i]);
        L->norm[i] = nv;
        if (nv >= L->on_thresh) { L->on_mask |= (1 << i); on_cnt++; }
        num  += L->pos[i] * (float)nv;
        wsum += nv;
    }

    /* 全部在线 = 十字路口 / 停止线 / 终点区。
     * 单独成一个状态(而不是"偏差=0 继续直行")的理由: 它在题目里通常是**要计数或要停车的事件**
     * (数第几个路口 / 见停止线停)。若混进 OK, 上层永远看不到这个事件。 */
    if (on_cnt == L->n) {
        L->lost_ms = 0;
        L->err = 0.0f;
        L->last_err = 0.0f;
        if (err_out) *err_out = 0.0f;
        if (w_out)   *w_out = 0;      /* 路口不猜方向: 直行由上层给 v, 转向交给上层策略 */
        return LINE_CROSS;
    }

    /* 全部离线 = 脱线。输出"往最后已知方向搜"的转向 —— 线是从某一侧丢的, 往那一侧转回去
     * 才能重新压上。**但上层必须自己看 lost_ms 决定何时放弃**(见 line.h 的说明:
     * 一直转下去是重新找到线的办法, 也是开出场地的办法)。 */
    if (on_cnt == 0) {
        L->lost_ms += (uint32_t)(dt_s * 1000.0f);
        if (w_out) *w_out = (int)(L->last_dir * L->search_w);
        if (err_out) *err_out = L->err;      /* 保留丢线前那一刻的偏差, 便于事后判断从哪边丢的 */
        return LINE_LOST;
    }

    L->lost_ms = 0;

    /* 加权质心: err = Σ(pos_i × norm_i) / Σ(norm_i)。用**全部通道的归一化值当权重**(不是只用
     * 过阈值的那几个): 这样线压在两个探头之间时 err 是连续变化的, 不会在阈值边界上跳一格 ——
     * 跳变会被 D 项放大成抽动。 */
    err = (wsum > 0) ? (num / (float)wsum) : 0.0f;

    /* err>0 = 线在左 -> 左转 -> w>0 (全工程约定)。D 项对偏差变化率取阻尼, 抑制蛇行。 */
    if (dt_s > 1e-6f) wi = (int)(L->kp * err + L->kd * (err - L->last_err) / dt_s);
    else              wi = (int)(L->kp * err);
    if (wi >  (int)L->w_max) wi =  (int)L->w_max;
    if (wi < -(int)L->w_max) wi = -(int)L->w_max;

    L->last_err = err;
    L->err = err;
    if (err > 0.0f)      L->last_dir =  1.0f;   /* 记住线在哪一侧, 丢线时往这边找 */
    else if (err < 0.0f) L->last_dir = -1.0f;

    if (err_out) *err_out = err;
    if (w_out)   *w_out = wi;
    return LINE_OK;
}
