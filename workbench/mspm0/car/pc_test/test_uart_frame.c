/*
 * test_uart_frame.c - 视觉串口帧解析(uart_frame.c) + 视觉伺服控制律(vservo.h) 的 PC 单元测试
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -I.. -o test_uart_frame test_uart_frame.c ../uart_frame.c
 *   ./test_uart_frame        (Windows: .\test_uart_frame.exe)
 *
 * 为什么这份测试非做不可：
 *   uart_frame.h 在 2026-07-27 之前是个**孤儿头文件** —— 没有 .c、不在 makefile、它自称的
 *   pc_test 也不存在，于是"视觉串口链已就绪"是纯假象。补实现的同时必须补断言，否则只是把
 *   假象从"没有代码"升级成"有一份没人验过的代码"。
 *
 * 覆盖的是**与相机无关**的那一半：帧同步、校验、字段严格性、新鲜度、失败分类、伺服符号与状态机。
 * 留给真机的只剩"相机吐的帧长什么样"和"增益整定"。
 */
#include "uart_frame.h"
#include "vservo.h"
#include <stdio.h>

static int g_fail = 0;

static void ck(const char *name, long got, long want)
{
    if (got == want) printf("  [PASS] %-38s got=%ld want=%ld\n", name, got, want);
    else           { printf("  [FAIL] %-38s got=%ld want=%ld\n", name, got, want); g_fail++; }
}
static void ck_true(const char *name, int cond)
{
    if (cond) printf("  [PASS] %-38s\n", name);
    else    { printf("  [FAIL] %-38s\n", name); g_fail++; }
}

/* 把字符串逐字节喂进解析器；返回"解析成功的帧数" */
static int feed(uf_parser_t *p, const char *s, uint32_t now)
{
    int n = 0;
    for (; *s; s++) if (uf_push(p, *s, now)) n++;
    return n;
}

/* 给任意 body 配上**正确**的校验和，拼成一帧。
 * ⚠ 测试坏帧时必须用它 —— 校验和是在字段解析**之前**判的，所以一个校验和瞎写的帧永远被归成
 *   n_bad_csum，根本走不到字段检查。这个顺序是对的（校验错=线路/发端算错，格式错=发端逻辑错，
 *   两者修法完全不同），但它意味着"要测格式错，就得给一个校验和正确的坏帧"。 */
static void build_body(char *dst, const char *body)
{
    uint16_t n = 0;
    while (body[n]) n++;
    snprintf(dst, 96, "$%s*%02X\n", body, uf_checksum(body, n));
}

/* 按格式拼一帧（含正确校验和），写进 dst */
static void build(char *dst, int id, int cx, int cy, int area)
{
    char body[64];
    snprintf(body, sizeof(body), "V,%d,%d,%d,%d", id, cx, cy, area);
    build_body(dst, body);
}

/* ============ 1. 正常帧 ============ */
static void test_good_frame(void)
{
    printf("test_good_frame:\n");
    uf_parser_t p; uf_init(&p);
    char f[96]; build(f, 1, 320, 240, 1500);

    ck("解析出 1 帧", feed(&p, f, 1000), 1);
    ck("n_ok", (long)p.n_ok, 1);
    ck("n_bad_form", (long)p.n_bad_form, 0);
    ck("n_bad_csum", (long)p.n_bad_csum, 0);

    uf_target_t t;
    ck_true("uf_get 成功", uf_get(&p, 1000, &t) == 1);
    ck("id", t.id, 1);
    ck("cx", t.cx, 320);
    ck("cy", t.cy, 240);
    ck("area", t.area, 1500);
    ck("status==UF_OK", uf_status(&p, 1000), UF_OK);

    /* 小写 hex 校验也要收(发端用 %02x 是常见写法) */
    uf_parser_t q; uf_init(&q);
    char body[] = "V,2,100,200,300";
    char low[96];
    snprintf(low, sizeof(low), "$%s*%02x\n", body, uf_checksum(body, (uint16_t)(sizeof(body) - 1)));
    ck("小写 hex 校验和也接受", feed(&q, low, 10), 1);
}

/* ============ 2. "没看到目标"必须与"掉线"区分开 ============ */
static void test_no_target_vs_stale(void)
{
    printf("test_no_target_vs_stale (本测试是整个设计的要点):\n");
    uf_parser_t p; uf_init(&p);

    ck("一开始 status==NO_DATA", uf_status(&p, 0), UF_NO_DATA);
    ck_true("NO_DATA 时 uf_get 必须失败", uf_get(&p, 0, 0) == 0);

    char f[96]; build(f, UF_ID_NONE, 0, 0, 0);
    ck("空目标帧仍是有效帧", feed(&p, f, 5000), 1);
    /* ★ 关键: 空目标帧**照样刷新时间戳** => 链路是好的, 只是没看到东西。
     * 若不刷新, 一台"视野里没目标"的正常相机会被误判成掉线, 现场会去查线 —— 白查。 */
    ck("status==NO_TARGET(不是 STALE)", uf_status(&p, 5000), UF_NO_TARGET);
    ck_true("uf_fresh==1(模块活着)", uf_fresh(&p, 5000) == 1);
    ck_true("但 uf_get 仍返回 0(没东西可伺服)", uf_get(&p, 5000, 0) == 0);

    /* 有目标, 然后停止发送 -> 过期 */
    build(f, 3, 100, 100, 900);
    feed(&p, f, 6000);
    ck("刚收到 -> OK", uf_status(&p, 6000), UF_OK);
    ck("边界内(=STALE_MS) 仍 OK", uf_status(&p, 6000 + UF_STALE_MS), UF_OK);
    ck("超一格 -> STALE", uf_status(&p, 6000 + UF_STALE_MS + 1), UF_STALE);
    /* 这一条是"相机掉线后不许按旧坐标继续开"的直接保证 */
    ck_true("STALE 时 uf_get 必须失败(过期数据不冒充有效)", uf_get(&p, 6000 + UF_STALE_MS + 1, 0) == 0);
}

/* ============ 3. 坏帧分类：修法不同的故障必须分开计数 ============ */
static void test_bad_frames(void)
{
    printf("test_bad_frames:\n");
    uf_parser_t p; uf_init(&p);

    /* 校验和错(线路噪声/发端算错) -> n_bad_csum */
    ck("校验错不产出帧", feed(&p, "$V,1,10,20,30*00\n", 100), 0);
    ck("n_bad_csum==1", (long)p.n_bad_csum, 1);
    ck("n_bad_form 不受影响", (long)p.n_bad_form, 0);
    ck_true("坏帧不得污染 have_frame", uf_status(&p, 100) == UF_NO_DATA);

    /* 校验和"位数不对/不是 hex"属于格式错, 不是校验错(它连校验都没法算) */
    uf_init(&p);
    feed(&p, "$V,1,10,20,30*3\n",  100);       /* 校验只有 1 位 */
    feed(&p, "$V,1,10,20,30*3AB\n", 100);      /* 校验 3 位 */
    feed(&p, "$V,1,10,20,30*GG\n", 100);       /* 非 hex 字符 */
    ck("校验段本身写坏 -> bad_form", (long)p.n_bad_form, 3);
    ck("这三种不算 bad_csum", (long)p.n_bad_csum, 0);

    /* 校验和**正确**但内容坏 -> n_bad_form(发端逻辑错, 与线路噪声要分开) */
    uf_init(&p);
    {
        const char *bad[] = {
            "V,1,10,20,30,40",   /* 字段太多 */
            "V,1,10,20",         /* 字段太少 */
            "V,1,1x,20,30",      /* 非数字 */
            "X,1,10,20,30",      /* 标识不是 V */
            "V,,10,20,30",       /* 空字段 */
            "V,-,10,20,30",      /* 只有一个负号: 宽松解析会读成 0, 必须拒 */
            "VV,1,10,20,30"      /* 标识长度不对 */
        };
        int i; char f[96];
        for (i = 0; i < 7; i++) { build_body(f, bad[i]); feed(&p, f, 100); }
    }
    feed(&p, "$V,1,10,20,30\n", 100);          /* 完全没有校验段 */
    ck("8 种格式错全部计入 n_bad_form", (long)p.n_bad_form, 8);
    ck("一个都没被算成校验错", (long)p.n_bad_csum, 0);
    ck("没有一帧被当成有效", (long)p.n_ok, 0);
    /* 严格解析的意义: 宽松解析会把 "1x" 读成 1、把空字段读成 0 —— 于是一个坏帧变成
     * "一个看起来合法的坐标", 车会朝不存在的目标开过去。宁可整帧丢掉。 */

    /* 超长没等到 '\n' -> n_overflow(修法是去核对波特率, 不是改帧格式) */
    uf_init(&p);
    {
        char junk[UF_BUF_LEN + 20];
        int i; junk[0] = '$';
        for (i = 1; i < (int)sizeof(junk) - 1; i++) junk[i] = 'A';
        junk[sizeof(junk) - 1] = 0;
        feed(&p, junk, 100);
        ck("n_overflow==1", (long)p.n_overflow, 1);
    }
}

/* ============ 4. 重新同步：截断帧后面跟着好帧，好帧必须能收到 ============ */
static void test_resync(void)
{
    printf("test_resync (半截帧不许和下一帧拼起来):\n");
    uf_parser_t p; uf_init(&p);
    char good[96]; build(good, 7, 400, 300, 2200);

    /* 先来半截(掉字节/发端复位), 紧接一个完整帧 */
    feed(&p, "$V,9,88,7", 200);
    ck("好帧被正确解析", feed(&p, good, 210), 1);
    uf_target_t t;
    ck_true("拿到的是好帧的数据", uf_get(&p, 210, &t) == 1);
    ck("id 来自好帧", t.id, 7);
    ck("cx 来自好帧", t.cx, 400);
    ck("截断被记为格式错", (long)p.n_bad_form, 1);
    /* 若把两个半截拼起来, 会得到一个数值合法但完全错误的坐标 —— 比丢帧危险得多 */

    /* 帧外的噪声(遥测/日志混在同一条线上)必须被忽略 */
    uf_init(&p);
    feed(&p, "[ctl] IDLE tgt=0 | I:0,0\n随便的垃圾\r\n", 300);
    ck("帧外字节全部忽略, 零计数", (long)(p.n_ok + p.n_bad_form + p.n_bad_csum + p.n_overflow), 0);
    ck("仍然 NO_DATA", uf_status(&p, 300), UF_NO_DATA);
    ck("此后好帧照收", feed(&p, good, 310), 1);
}

/* ============ 5. 视觉伺服控制律：符号与状态机 ============ */
static void test_vservo(void)
{
    printf("test_vservo (vservo.h 控制律):\n");
    vs_cfg_t c;
    c.center_x = 320; c.tol_px = 20;
    c.kp_w = 0.25f;   c.w_max = 60.0f;
    c.area_stop = 4000; c.kp_v = 0.02f; c.v_max = 80; c.v_min = 35;
    int v, w;

    /* 没有新鲜目标 -> 必须停。这是唯一会把车开进墙的失效模式, 所以单列一条 */
    ck("无目标 -> VS_LOST", vs_step(&c, 0, 0, 0, &v, &w), VS_LOST);
    ck("无目标 v==0", v, 0);
    ck("无目标 w==0", w, 0);

    /* 目标偏左(cx<center) -> 左转 -> w>0 (与 car_drive_mix 的 w>0=左转 一致) */
    ck("目标偏左 -> VS_TURNING", vs_step(&c, 1, 200, 100, &v, &w), VS_TURNING);
    ck_true("偏左 w>0(左转去追)", w > 0);
    ck("对准阶段不前进", v, 0);

    /* 目标偏右 -> 右转 -> w<0 */
    ck("目标偏右 -> VS_TURNING", vs_step(&c, 1, 440, 100, &v, &w), VS_TURNING);
    ck_true("偏右 w<0(右转去追)", w < 0);

    /* 差速上限 */
    vs_step(&c, 1, 0, 100, &v, &w);
    ck("w 被限幅", w, (long)c.w_max);

    /* 已对准但还远 -> 前进 */
    ck("对准且远 -> VS_APPROACH", vs_step(&c, 1, 320, 1000, &v, &w), VS_APPROACH);
    ck_true("前进速度在 [v_min, v_max]", v >= c.v_min && v <= c.v_max);

    /* 很近但还没到停止面积 -> 速度不许低于死区(否则卡在"快到了"干嗡嗡) */
    vs_step(&c, 1, 320, 3990, &v, &w);
    ck("接近末端速度被抬到 v_min", v, (long)c.v_min);

    /* 对准 + 够近 -> 停, 交给上层动手(吸球) */
    ck("对准且够近 -> VS_ALIGNED", vs_step(&c, 1, 320, 4000, &v, &w), VS_ALIGNED);
    ck("ALIGNED v==0", v, 0);
    ck("ALIGNED w==0", w, 0);
}

/* ============ 6. 端到端：帧 -> 伺服指令 ============ */
static void test_end_to_end(void)
{
    printf("test_end_to_end (串口字节 -> 伺服指令):\n");
    uf_parser_t p; uf_init(&p);
    vs_cfg_t c;
    c.center_x = 320; c.tol_px = 20; c.kp_w = 0.25f; c.w_max = 60.0f;
    c.area_stop = 4000; c.kp_v = 0.02f; c.v_max = 80; c.v_min = 35;

    char f[96]; build(f, 1, 120, 240, 900);      /* 目标在左边、还远 */
    feed(&p, f, 1000);

    uf_target_t t; t.id = 0; t.cx = 0; t.cy = 0; t.area = 0; t.stamp_ms = 0;
    int have = uf_get(&p, 1000, &t);
    int v, w;
    vs_state_t st = vs_step(&c, have, (int)t.cx, (int)t.area, &v, &w);
    ck_true("拿到目标", have == 1);
    ck("先转向对准", st, VS_TURNING);
    ck_true("往左转", w > 0);

    /* 相机掉线 200ms 后: 同一份数据必须变成"停车" */
    have = uf_get(&p, 1000 + UF_STALE_MS + 1, &t);
    st = vs_step(&c, have, (int)t.cx, (int)t.area, &v, &w);
    ck_true("掉线后拿不到目标", have == 0);
    ck("掉线 -> VS_LOST", st, VS_LOST);
    ck("掉线 -> 停车", v + w, 0);
}

int main(void)
{
    printf("==== test_uart_frame (视觉链: 帧解析 + 伺服控制律) ====\n");
    printf("UF_BUF_LEN=%d  UF_STALE_MS=%d\n\n", (int)UF_BUF_LEN, (int)UF_STALE_MS);
    test_good_frame();
    test_no_target_vs_stale();
    test_bad_frames();
    test_resync();
    test_vservo();
    test_end_to_end();
    printf("\n==== %s ====\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    if (g_fail) printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
