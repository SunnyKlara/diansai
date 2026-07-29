/*
 * test_disp_run.c —— disp_run.c 的 PC 单测
 *   gcc -std=c99 -I.. -o disp.exe test_disp_run.c ../disp_run.c && ./disp.exe
 *
 * 重点：
 *   ① 每行**定长右补空格** —— 覆盖式绘制靠它擦掉上一帧残字（"1:02.3" -> "9.9" 不许留 "9.92.3"）
 *   ② diff 位掩码只标真变了的行（这是"每帧只重画变化行"的依据，历史上 LCD 全屏重绘拖慢过主循环）
 *   ③ 无效球位显示 "ball --" 而不是 "0.0"（0.0 会被误读成"球正好在中心"）
 */
#include <stdio.h>
#include <string.h>
#include "disp_run.h"

static int g_ok = 0, g_bad = 0;

static void cks(const char *name, const char *got, const char *want)
{
    int good = (strcmp(got, want) == 0);
    printf("  %-40s got=\"%s\" want=\"%s\" %s\n", name, got, want, good ? "OK" : "**FAIL**");
    if (good) g_ok++; else g_bad++;
}

static void ck(const char *name, long got, long want)
{
    int good = (got == want);
    printf("  %-40s got=%-10ld want=%-10ld %s\n", name, got, want, good ? "OK" : "**FAIL**");
    if (good) g_ok++; else g_bad++;
}

/* 去掉右侧补的空格，便于对比可读的期望值 */
static const char *trim(const char *s)
{
    static char b[DISP_RUN_LEN];
    int n;
    strncpy(b, s, DISP_RUN_LEN - 1);
    b[DISP_RUN_LEN - 1] = '\0';
    n = (int)strlen(b);
    while (n > 0 && b[n - 1] == ' ') b[--n] = '\0';
    return b;
}

static disp_run_in_t mk(int st, int fail, uint32_t ms, float d, float ball,
                        uint32_t runs, uint32_t lost)
{
    disp_run_in_t in;
    in.state = st; in.fail = fail; in.elapsed_ms = ms;
    in.dist_mm = d; in.ball_mm = ball; in.n_runs = runs; in.n_lost = lost;
    return in;
}

int main(void)
{
    disp_run_txt_t a, b;
    disp_run_in_t in;

    printf("-- 1. 各行格式\n");
    in = mk(1, 0, 12345u, 3141.6f, 4.24f, 2u, 0u);
    disp_run_build(&in, &a);
    cks("L0 走时", trim(a.line[0]), "12.3");
    cks("L1 状态", trim(a.line[1]), "RUN");
    cks("L2 失败(非 ABORT 时留空)", trim(a.line[2]), "");
    cks("L3 里程", trim(a.line[3]), "3.14m");
    cks("L4 球位(带正号)", trim(a.line[4]), "b+4.2");
    cks("L5 跑次/丢线", trim(a.line[5]), "#2 L0");

    printf("-- 2. 走时跨 60s 变 M:SS.S\n");
    in = mk(1, 0, 62345u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("62345ms", trim(a.line[0]), "1:02.3");
    in = mk(1, 0, 59999u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("59999ms", trim(a.line[0]), "59.9");
    in = mk(0, 0, 0u, 0.0f, 0.0f, 0u, 0u);
    disp_run_build(&in, &a);
    cks("0ms", trim(a.line[0]), "0.0");
    cks("IDLE -> READY", trim(a.line[1]), "READY");

    printf("-- 3. 🔴 定长右补空格：长内容换短内容不许留残字\n");
    in = mk(1, 0, 62345u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    ck("长内容行长 = DISP_RUN_LEN-1", (long)strlen(a.line[0]), (long)(DISP_RUN_LEN - 1));
    in = mk(1, 0, 9900u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &b);
    ck("短内容行长也 = DISP_RUN_LEN-1", (long)strlen(b.line[0]), (long)(DISP_RUN_LEN - 1));
    cks("短内容 trim 后干净", trim(b.line[0]), "9.9");
    ck("第 5 个字符是空格(残字被覆盖)", b.line[0][5] == ' ', 1);

    printf("-- 4. 失败原因只在 ABORT 时出现\n");
    in = mk(4, 1, 40000u, 3000.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("ABORT+TIMEOUT", trim(a.line[2]), "TIMEOUT");
    cks("状态 = ABORT", trim(a.line[1]), "ABORT");
    in = mk(4, 2, 4000u, 300.0f, 0.0f, 1u, 3u);
    disp_run_build(&in, &a);
    cks("ABORT+LOST", trim(a.line[2]), "LOST");
    cks("丢线计数出现在 L5", trim(a.line[5]), "#1 L3");
    in = mk(3, 0, 18300u, 6141.6f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("DONE 时 L2 留空", trim(a.line[2]), "");
    cks("DONE 状态", trim(a.line[1]), "DONE");
    cks("整圈里程", trim(a.line[3]), "6.14m");

    printf("-- 5. 球位：无效 -> \"ball --\"，负值带负号\n");
    in = mk(1, 0, 0u, 0.0f, DISP_RUN_NO_BALL, 1u, 0u);
    disp_run_build(&in, &a);
    cks("无效球位", trim(a.line[4]), "ball --");
    in = mk(1, 0, 0u, 0.0f, -7.85f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("负球位", trim(a.line[4]), "b-7.9");
    in = mk(1, 0, 0u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    cks("球在中心显示 +0.0(不是 --)", trim(a.line[4]), "b+0.0");

    printf("-- 6. diff：只标真变了的行\n");
    in = mk(1, 0, 12345u, 3000.0f, 1.0f, 2u, 0u);
    disp_run_build(&in, &a);
    ck("prev=NULL -> 全画", (long)disp_run_diff(0, &a), (long)((1u << DISP_RUN_LINES) - 1u));
    ck("自己跟自己 -> 0", (long)disp_run_diff(&a, &a), 0);
    in = mk(1, 0, 12445u, 3000.0f, 1.0f, 2u, 0u);       /* 只走时变了 */
    disp_run_build(&in, &b);
    ck("只走时变 -> 只有 bit0", (long)disp_run_diff(&a, &b), 1);
    in = mk(1, 0, 12445u, 3000.0f, 1.0f, 2u, 0u);
    in.elapsed_ms = 12445u; in.dist_mm = 3200.0f;        /* 走时 + 里程都变 */
    disp_run_build(&in, &b);
    ck("走时+里程 -> bit0|bit3", (long)disp_run_diff(&a, &b), 1 | 8);
    in = mk(4, 2, 12445u, 3200.0f, 1.0f, 2u, 1u);        /* 状态/失败/元信息也变 */
    disp_run_build(&in, &b);
    ck("四行变 -> bit0|1|2|3|5", (long)disp_run_diff(&a, &b), 1 | 2 | 4 | 8 | 32);

    printf("-- 7. 走时同一个 0.1s 内不该产生重画（省 LCD 带宽）\n");
    in = mk(1, 0, 12300u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &a);
    in = mk(1, 0, 12399u, 0.0f, 0.0f, 1u, 0u);
    disp_run_build(&in, &b);
    ck("12300 与 12399 同帧 -> diff=0", (long)disp_run_diff(&a, &b), 0);

    printf("-- 8. 边界：不许越界（每行都以 '\\0' 收尾）\n");
    in = mk(4, 3, 5999999u, 999999.0f, -123.4f, 999u, 999u);
    disp_run_build(&in, &a);
    {
        int i, bad = 0;
        for (i = 0; i < DISP_RUN_LINES; i++)
            if (a.line[i][DISP_RUN_LEN - 1] != '\0') bad++;
        ck("6 行全部正确收尾", bad, 0);
    }
    printf("     (极端值实际渲染: |%s|%s|%s|%s|%s|%s|)\n",
           trim(a.line[0]), trim(a.line[1]), trim(a.line[2]),
           trim(a.line[3]), trim(a.line[4]), trim(a.line[5]));

    printf("\n  passed=%d  failed=%d\n", g_ok, g_bad);
    printf("%s\n", g_bad == 0 ? "==== ALL PASS ====" : "==== FAIL ====");
    return g_bad ? 1 : 0;
}
