/*
 * test_line.c - 光电循迹算法层(line.c) 的 PC 单元测试（阶梯 5）
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -I.. -o test_line test_line.c ../line.c
 *   ./test_line          (Windows: .\test_line.exe)
 *
 * 传感器还没到手，所以这份测试就是循迹现在**唯一**能拿到的证据。它验的是与硬件无关的那半：
 *   标定与归一化（含"黑读数更低"的反极性传感器）、对比度不足的拒绝、质心符号、
 *   路口/丢线两个事件、丢线方向记忆、限幅。
 * 留给真机的只剩"探头几何 + 增益整定 + 现场按两下标定"。
 */
#include "line.h"
#include <stdio.h>

static int g_fail = 0;

static void ck(const char *name, long got, long want)
{
    if (got == want) printf("  [PASS] %-40s got=%ld want=%ld\n", name, got, want);
    else           { printf("  [FAIL] %-40s got=%ld want=%ld\n", name, got, want); g_fail++; }
}
static void ck_true(const char *name, int cond)
{
    if (cond) printf("  [PASS] %-40s\n", name);
    else    { printf("  [FAIL] %-40s\n", name); g_fail++; }
}

/* 5 路，白=3000 黑=500（**黑读数更低**，反射式光电的常见极性） */
static void cal5(line_t *L)
{
    int w[5] = { 3000, 3000, 3000, 3000, 3000 };
    int b[5] = {  500,  500,  500,  500,  500 };
    line_init(L, 5, 0);
    line_cal_white(L, w);
    line_cal_black(L, b);
}

/* ============ 1. 没标定就必须拒绝输出转向 ============ */
static void test_needs_calibration(void)
{
    printf("test_needs_calibration:\n");
    line_t L; line_init(&L, 5, 0);
    int raw[5] = { 3000, 3000, 500, 3000, 3000 };
    float err = 9.0f; int w = 99;

    ck("未标定 -> LINE_NOCAL", line_step(&L, raw, 0.02f, &err, &w), LINE_NOCAL);
    ck("未标定 w 必须为 0", w, 0);
    ck_true("line_calibrated()==0", line_calibrated(&L) == 0);

    /* 只标了一半也不算标定过 */
    int wref[5] = { 3000, 3000, 3000, 3000, 3000 };
    line_cal_white(&L, wref);
    ck("只标白 -> 仍 NOCAL", line_step(&L, raw, 0.02f, &err, &w), LINE_NOCAL);
}

/* ============ 2. 对比度不足 = 反馈不可信，必须报出来 ============ */
static void test_contrast_guard(void)
{
    printf("test_contrast_guard (这条是'先证反馈可信再碰控制器'的落地):\n");
    line_t L; line_init(&L, 5, 0);
    int w5[5] = { 3000, 3000, 3000, 3000, 3000 };
    int b5[5] = {  500,  500, 2950,  500,  500 };   /* 3 号探头白黑几乎一样(离地太高/坏了) */
    line_cal_white(&L, w5);
    line_cal_black(&L, b5);

    ck_true("有坏通道 -> 判未标定", line_calibrated(&L) == 0);
    ck("指出是第 2 号通道", line_bad_channel(&L), 2);
    int raw[5] = { 3000, 3000, 500, 3000, 3000 };
    float err; int w = 77;
    ck("坏通道 -> LINE_NOCAL", line_step(&L, raw, 0.02f, &err, &w), LINE_NOCAL);
    ck("并且不输出转向", w, 0);
    /* 若不做这道门, 该通道归一化出来是噪声放大 (raw-3000)*1000/(-50) —— 一点噪声就是满量程,
     * 结果是车抽风, 而人会以为"PID 没调好"去调增益, 方向完全错。 */

    /* 修好那个探头(重新标定) -> 恢复可用 */
    int b_ok[5] = { 500, 500, 500, 500, 500 };
    line_cal_black(&L, b_ok);
    ck_true("重标后可用", line_calibrated(&L) == 1);
    ck("bad_channel 返回 -1", line_bad_channel(&L), -1);
}

/* ============ 3. 归一化：兼容"黑读数更低"的反极性 ============ */
static void test_normalize(void)
{
    printf("test_normalize (不需要'极性'配置项):\n");
    line_t L; cal5(&L);
    ck("白底 -> 0",     line_normalize(&L, 0, 3000), 0);
    ck("线上 -> 1000",  line_normalize(&L, 0,  500), 1000);
    ck("中间 -> ~500",  line_normalize(&L, 0, 1750), 500);
    ck("超白端被限幅",  line_normalize(&L, 0, 4000), 0);
    ck("超黑端被限幅",  line_normalize(&L, 0,    0), 1000);

    /* 正极性(黑读数更高)也要对: 少一个能配错的开关 */
    line_t P; line_init(&P, 3, 0);
    int wp[3] = { 100, 100, 100 }, bp[3] = { 3900, 3900, 3900 };
    line_cal_white(&P, wp); line_cal_black(&P, bp);
    ck("正极性 白 -> 0",    line_normalize(&P, 0, 100), 0);
    ck("正极性 黑 -> 1000", line_normalize(&P, 0, 3900), 1000);
}

/* ============ 4. 质心符号：线在左 -> err>0 -> 左转 w>0 ============ */
static void test_centroid_sign(void)
{
    printf("test_centroid_sign (符号搞反就会朝反方向跑飞):\n");
    line_t L; cal5(&L);
    float err; int w;

    /* raw[0] 是最左边的探头。默认 pos = +24,+12,0,-12,-24 (左为正, 间距 12mm) */
    int left[5]   = {  500, 3000, 3000, 3000, 3000 };   /* 线压在最左探头 */
    int center[5] = { 3000, 3000,  500, 3000, 3000 };   /* 线压在中间探头 */
    int right[5]  = { 3000, 3000, 3000, 3000,  500 };   /* 线压在最右探头 */

    ck("线在左 -> LINE_OK", line_step(&L, left, 0.02f, &err, &w), LINE_OK);
    ck_true("线在左 err>0", err > 0.0f);
    ck_true("线在左 w>0(左转去追)", w > 0);
    printf("        [info] 线在最左: err=%.1fmm w=%d\n", err, w);

    line_step(&L, center, 0.02f, &err, &w);
    ck_true("线居中 err≈0", err > -1.0f && err < 1.0f);

    line_step(&L, right, 0.02f, &err, &w);
    ck_true("线在右 err<0", err < 0.0f);
    ck_true("线在右 w<0(右转去追)", w < 0);

    /* 线压在两个探头之间 -> err 应落在两者之间(连续), 不是跳一格 */
    int between[5] = { 3000, 500, 500, 3000, 3000 };
    line_step(&L, between, 0.02f, &err, &w);
    printf("        [info] 线跨 1/2 号探头: err=%.1fmm\n", err);
    ck_true("跨探头时 err 落在两者之间(连续变化)", err > 3.0f && err < 15.0f);

    /* 限幅 */
    line_t H; cal5(&H); H.kp = 100.0f; H.kd = 0.0f;
    line_step(&H, left, 0.02f, &err, &w);
    ck("w 被限幅到 w_max", w, (long)H.w_max);
}

/* ============ 5. 路口(全黑)必须是独立事件 ============ */
static void test_cross(void)
{
    printf("test_cross (全黑=路口/停止线, 不能混进 OK):\n");
    line_t L; cal5(&L);
    int all_black[5] = { 500, 500, 500, 500, 500 };
    float err; int w = 5;
    ck("全黑 -> LINE_CROSS", line_step(&L, all_black, 0.02f, &err, &w), LINE_CROSS);
    ck("路口不猜转向 w=0", w, 0);
    /* 若混进 LINE_OK, 上层就永远看不到"路过第几个路口"这个事件 —— 而那常常正是评分点
     * (数路口 / 见停止线停车)。 */
}

/* ============ 6. 丢线：往最后已知方向搜 + 计时 ============ */
static void test_lost(void)
{
    printf("test_lost (丢线要往丢的那一侧找回来):\n");
    line_t L; cal5(&L);
    int right[5]  = { 3000, 3000, 3000, 3000,  500 };   /* 先让线在右边 */
    int all_white[5] = { 3000, 3000, 3000, 3000, 3000 };
    float err; int w;

    line_step(&L, right, 0.02f, &err, &w);              /* 记住 last_dir = 右 */
    ck("全白 -> LINE_LOST", line_step(&L, all_white, 0.02f, &err, &w), LINE_LOST);
    ck_true("往右搜(w<0, 与丢线方向一致)", w < 0);
    ck("搜索幅值 = search_w", -w, (long)L.search_w);

    /* 丢线计时累加, 供上层决定何时放弃(一直转下去既是找回线的办法, 也是开出场地的办法) */
    int i; for (i = 0; i < 10; i++) line_step(&L, all_white, 0.05f, &err, &w);
    ck("lost_ms 累加(20ms + 10*50ms)", (long)L.lost_ms, 520);

    /* 重新压上线 -> 计时归零 */
    line_step(&L, right, 0.02f, &err, &w);
    ck("重新找到线后 lost_ms 归零", (long)L.lost_ms, 0);

    /* 反向: 线在左边丢 -> 往左搜 */
    line_t M; cal5(&M);
    int left[5] = { 500, 3000, 3000, 3000, 3000 };
    line_step(&M, left, 0.02f, &err, &w);
    line_step(&M, all_white, 0.02f, &err, &w);
    ck_true("从左边丢 -> 往左搜(w>0)", w > 0);
}

int main(void)
{
    printf("==== test_line (光电循迹算法层) ====\n");
    printf("LINE_MAX_CH=%d\n\n", (int)LINE_MAX_CH);
    test_needs_calibration();
    test_contrast_guard();
    test_normalize();
    test_centroid_sign();
    test_cross();
    test_lost();
    printf("\n==== %s ====\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    if (g_fail) printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
