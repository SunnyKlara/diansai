/*
 * test_task.c —— task.c 的 PC 单测（不需要板子）
 *   gcc -std=c99 -I.. -o task.exe test_task.c ../task.c && ./task.exe
 *
 * 断言的重点不是"函数会不会跑"，而是**把已知的失败模式写成断言**：
 *   · 上电时按键被按住 -> 不许自己起跑
 *   · 起点就压在启停线上 -> 不许一启动就判定到终点（本层头号翻车点）
 *   · 只有 cross 没有里程 / 只有里程没有 cross -> 都不算到达
 *   · DONE 之后走时必须冻结（否则屏上数字一直涨，评委会当成没停）
 *   · 运行中必须周期性要求刷屏（说明 5 要求实时走时，这是判分件）
 */
#include <stdio.h>
#include <string.h>
#include "task.h"

static int g_ok = 0, g_bad = 0;

static void ck(const char *name, long got, long want)
{
    int good = (got == want);
    printf("  %-52s got=%-10ld want=%-10ld %s\n", name, got, want, good ? "OK" : "**FAIL**");
    if (good) g_ok++; else g_bad++;
}

/* 一个便利壳：喂一拍 */
static task_out_t step(task_t *T, uint32_t ms, int btn, int lost, int cross,
                       float dist, int stopped)
{
    task_in_t in;
    task_out_t out;
    memset(&out, 0, sizeof out);
    in.now_ms = ms; in.btn = btn; in.line_lost = lost; in.line_cross = cross;
    in.dist_mm = dist; in.stopped = stopped;
    task_step(T, &in, &out);
    return out;
}

/* 从 t0 起把按键按住/松开 n 毫秒（每 1ms 一拍），返回期间是否出现过起跑 */
static int hold(task_t *T, uint32_t *t, int btn, uint32_t n, float dist)
{
    uint32_t i;
    int fired_run = 0;
    for (i = 0; i < n; i++) {
        task_out_t o = step(T, (*t)++, btn, 0, 0, dist, 0);
        if (o.beep == TASK_BEEP_START) fired_run = 1;
    }
    return fired_run;
}

int main(void)
{
    char buf[16];

    printf("-- 1. 按键消抖：短抖动不许触发，稳定 20ms 才触发\n");
    {
        task_t T; uint32_t t = 1000;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f);                       /* 先稳定松开 -> armed */
        ck("15ms 抖动不触发", hold(&T, &t, 1, 15, 0.0f), 0);
        hold(&T, &t, 0, 30, 0.0f);
        ck("稳定 25ms 触发一次", hold(&T, &t, 1, 25, 0.0f), 1);
        ck("触发后进 RUN", T.state, TASK_RUN);
    }

    printf("-- 2. 长按不重复触发；必须松开再按才算第二次\n");
    {
        task_t T; uint32_t t = 1000; int n = 0; uint32_t i;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f);
        for (i = 0; i < 500; i++) {                       /* 一直按住 0.5s */
            task_out_t o = step(&T, t++, 1, 0, 0, 0.0f, 0);
            if (o.beep == TASK_BEEP_START) n++;
        }
        ck("长按 500ms 只起跑 1 次", n, 1);
    }

    printf("-- 3. 🔴 上电时按键就被按住 -> 不许自己起跑\n");
    {
        task_t T; uint32_t t = 0;
        task_init(&T);
        ck("按住 200ms 仍在 IDLE", hold(&T, &t, 1, 200, 0.0f), 0);
        ck("state 仍 IDLE", T.state, TASK_IDLE);
        hold(&T, &t, 0, 30, 0.0f);                        /* 稳定松开 */
        ck("松开后再按才起跑", hold(&T, &t, 1, 30, 0.0f), 1);
    }

    printf("-- 4. 🔴 起点压在启停线上 -> 不许一启动就判到终点\n");
    {
        task_t T; uint32_t t = 1000; uint32_t i;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f);
        hold(&T, &t, 1, 30, 0.0f);
        ck("已起跑", T.state, TASK_RUN);
        for (i = 0; i < 200; i++) step(&T, t++, 0, 0, /*cross=*/1, 50.0f, 0);  /* 屏蔽窗内一直 cross */
        ck("屏蔽窗内 cross 被忽略", T.state, TASK_RUN);
    }

    printf("-- 5. 到达判定 = 双闸门（缺一不可）\n");
    {
        task_t T; uint32_t t = 1000;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        step(&T, t++, 0, 0, 0, 6000.0f, 0);
        ck("里程够但没 cross -> 仍 RUN", T.state, TASK_RUN);
        step(&T, t++, 0, 0, 1, 1000.0f, 0);
        ck("有 cross 但里程不够 -> 仍 RUN", T.state, TASK_RUN);
        step(&T, t++, 0, 0, 1, 6000.0f, 0);
        ck("两个都满足 -> BRAKE", T.state, TASK_BRAKE);
    }

    printf("-- 6. 预降速：过 TASK_SLOW_MM 后转 SLOW\n");
    {
        task_t T; uint32_t t = 1000; task_out_t o;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        o = step(&T, t++, 0, 0, 0, 1000.0f, 0);
        ck("巡航段 = CRUISE", o.v_mode, TASK_V_CRUISE);
        o = step(&T, t++, 0, 0, 0, 5900.0f, 0);
        ck("过 5800mm = SLOW", o.v_mode, TASK_V_SLOW);
        o = step(&T, t++, 0, 0, 1, 5900.0f, 0);
        ck("进 BRAKE 后 = STOP", o.v_mode, TASK_V_STOP);
    }

    printf("-- 7. 计时：RUN 中在涨，DONE 后冻结，且计时在'车停住'才停\n");
    {
        task_t T; uint32_t t = 1000; task_out_t o; uint32_t t_run0;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        t_run0 = t;                                    /* 起跑后第一拍的时刻 */
        o = step(&T, t_run0 + 12345, 0, 0, 0, 3000.0f, 0);
        ck("走时约等于经过时间(±30ms)", (o.elapsed_ms > 12300 && o.elapsed_ms < 12400), 1);
        o = step(&T, t_run0 + 12400, 0, 0, 1, 6000.0f, 0);
        ck("到达 -> BRAKE", o.state, TASK_BRAKE);
        o = step(&T, t_run0 + 12600, 0, 0, 0, 6000.0f, 0);
        ck("BRAKE 中走时仍在涨", (o.elapsed_ms > 12500), 1);
        o = step(&T, t_run0 + 12800, 0, 0, 0, 6000.0f, /*stopped=*/1);
        ck("车停住 -> DONE", o.state, TASK_DONE);
        ck("DONE 时走时约 12800(±60)", (o.elapsed_ms > 12740 && o.elapsed_ms < 12860), 1);
        {
            uint32_t frozen = o.elapsed_ms;
            o = step(&T, t_run0 + 20000, 0, 0, 0, 6000.0f, 1);
            ck("DONE 后走时冻结", (long)o.elapsed_ms, (long)frozen);
            ck("DONE 后 beep 不再响", o.beep, TASK_BEEP_NONE);
        }
    }

    printf("-- 8. BRAKE 兜底：没有 stopped 信号也要能进 DONE\n");
    {
        task_t T; uint32_t t = 1000; task_out_t o;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        step(&T, t, 0, 0, 1, 6000.0f, 0);
        o = step(&T, t + TASK_BRAKE_MS + 1, 0, 0, 0, 6000.0f, /*stopped=*/0);
        ck("超过 TASK_BRAKE_MS 判停住", o.state, TASK_DONE);
    }

    printf("-- 9. 失败要有名字：超时 / 丢线 / 手动\n");
    {
        task_t T; uint32_t t = 1000; task_out_t o;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        o = step(&T, t + TASK_MAX_MS + 1, 0, 0, 0, 3000.0f, 0);
        ck("超时 -> ABORT", o.state, TASK_ABORT);
        ck("fail = TIMEOUT", o.fail, TASK_FAIL_TIMEOUT);
        ck("ABORT 后 v_mode = STOP", o.v_mode, TASK_V_STOP);

        task_init(&T); t = 1000;
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        {   /* 连续丢线 */
            uint32_t i, t0 = t;
            for (i = 0; i < TASK_LOST_STOP_MS + 10; i++) step(&T, t0 + i, 0, 1, 0, 3000.0f, 0);
        }
        ck("连续丢线 -> ABORT", T.state, TASK_ABORT);
        ck("fail = LOST", T.fail, TASK_FAIL_LOST);

        task_init(&T); t = 1000;
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        task_abort(&T, t, TASK_FAIL_MANUAL);
        ck("外部急停 -> ABORT", T.state, TASK_ABORT);
        ck("fail = MANUAL", T.fail, TASK_FAIL_MANUAL);
    }

    printf("-- 10. 丢线未达门限又恢复 -> 不许误停（弯道短暂全白是常态）\n");
    {
        task_t T; uint32_t t = 1000; uint32_t i, t0;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        t0 = t;
        for (i = 0; i < 600; i++)  step(&T, t0 + i, 0, 1, 0, 3000.0f, 0);        /* 丢 600ms */
        for (i = 0; i < 50;  i++)  step(&T, t0 + 600 + i, 0, 0, 0, 3000.0f, 0);  /* 恢复 */
        for (i = 0; i < 900; i++)  step(&T, t0 + 650 + i, 0, 1, 0, 3000.0f, 0);  /* 又丢 900ms */
        ck("两段各自未超门限 -> 仍 RUN", T.state, TASK_RUN);
    }

    printf("-- 11. 刷屏节流：1s 内应有约 10 次 dirty（TASK_DISP_MS=100）\n");
    {
        task_t T; uint32_t t = 1000; uint32_t i, t0; int n = 0;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        t0 = t;
        for (i = 0; i < 1000; i++) if (step(&T, t0 + i, 0, 0, 0, 100.0f, 0).disp_dirty) n++;
        ck("dirty 次数在 9~11", (n >= 9 && n <= 11), 1);
    }

    printf("-- 12. 走时格式化\n");
    {
        task_fmt_time(0, buf, sizeof buf);      ck("0ms -> \"0.0\"",      strcmp(buf, "0.0"),      0);
        task_fmt_time(12345, buf, sizeof buf);  ck("12345 -> \"12.3\"",   strcmp(buf, "12.3"),     0);
        task_fmt_time(19999, buf, sizeof buf);  ck("19999 -> \"19.9\"",   strcmp(buf, "19.9"),     0);
        task_fmt_time(59999, buf, sizeof buf);  ck("59999 -> \"59.9\"",   strcmp(buf, "59.9"),     0);
        task_fmt_time(60000, buf, sizeof buf);  ck("60000 -> \"1:00.0\"", strcmp(buf, "1:00.0"),   0);
        task_fmt_time(62345, buf, sizeof buf);  ck("62345 -> \"1:02.3\"", strcmp(buf, "1:02.3"),   0);
        ck("buf 太小时返回 0", task_fmt_time(1000, buf, 4), 0);
    }

    printf("-- 13. 复位：DONE 后按一次回 IDLE，可跑下一趟且跑次累加\n");
    {
        task_t T; uint32_t t = 1000;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        step(&T, t, 0, 0, 1, 6000.0f, 1);
        step(&T, t + 1, 0, 0, 0, 6000.0f, 1);
        ck("第一趟 DONE", T.state, TASK_DONE);
        t += 10;
        hold(&T, &t, 0, 30, 6000.0f); hold(&T, &t, 1, 30, 6000.0f);
        ck("按一次回 IDLE", T.state, TASK_IDLE);
        hold(&T, &t, 0, 30, 6000.0f); hold(&T, &t, 1, 30, 6000.0f);
        ck("再按一次起第二趟", T.state, TASK_RUN);
        ck("跑次 = 2", (long)T.n_runs, 2);
        /* 关键：起跑时里程读数是 6000，本层自己做差 ⇒ 不该立刻满足里程闸门 */
        step(&T, t++, 0, 0, 1, 6000.0f, 0);
        ck("新趟里程自动归零(未误判到达)", T.state, TASK_RUN);
    }

    printf("-- 14. 32 位毫秒回绕附近不许乱（用差值判号的理由）\n");
    {
        task_t T; uint32_t t = 0xFFFFF000u; task_out_t o;
        task_init(&T);
        hold(&T, &t, 0, 30, 0.0f); hold(&T, &t, 1, 30, 0.0f);
        ck("回绕前能起跑", T.state, TASK_RUN);
        o = step(&T, t + 5000u, 0, 0, 0, 100.0f, 0);      /* 跨过 0 */
        ck("跨回绕后仍 RUN(未误判超时)", o.state, TASK_RUN);
        ck("走时约 5000(±40)", (o.elapsed_ms > 4960 && o.elapsed_ms < 5040), 1);
    }

    printf("\n  passed=%d  failed=%d\n", g_ok, g_bad);
    printf("%s\n", g_bad == 0 ? "==== ALL PASS ====" : "==== FAIL ====");
    return g_bad ? 1 : 0;
}
