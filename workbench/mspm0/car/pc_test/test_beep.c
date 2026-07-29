/*
 * test_beep.c —— beep.c 的 PC 单测
 *   gcc -std=c99 -I.. -o beep.exe test_beep.c ../beep.c && ./beep.exe
 *
 * 重点断言的是这类状态机的两个经典失败模式：
 *   ① 图案跑完忘了关 -> 蜂鸣器一直叫（现场会盖过评委说话）
 *   ② 只在"到期那一拍"输出 1、其余拍输出 0 -> 听不见（电平必须在整个 on 相位保持）
 */
#include <stdio.h>
#include "beep.h"

static int g_ok = 0, g_bad = 0;

static void ck(const char *name, long got, long want)
{
    int good = (got == want);
    printf("  %-50s got=%-8ld want=%-8ld %s\n", name, got, want, good ? "OK" : "**FAIL**");
    if (good) g_ok++; else g_bad++;
}

/* 从 t0 起跑 n 毫秒，统计高电平拍数与"上升沿次数"（= 响了几声） */
static void run(beep_t *B, uint32_t t0, uint32_t n, int *high, int *edges)
{
    uint32_t i;
    int prev = 0;
    *high = 0; *edges = 0;
    for (i = 0; i < n; i++) {
        int lv = beep_step(B, t0 + i);
        if (lv) (*high)++;
        if (lv && !prev) (*edges)++;
        prev = lv;
    }
}

int main(void)
{
    printf("-- 1. 空闲时恒静音\n");
    {
        beep_t B; int h, e;
        beep_init(&B);
        run(&B, 1000, 500, &h, &e);
        ck("500 拍全 0", h, 0);
        ck("busy = 0", beep_busy(&B), 0);
    }

    printf("-- 2. SHORT：一声，长度 = BEEP_SHORT_MS，之后回 0\n");
    {
        beep_t B; int h, e;
        beep_init(&B);
        beep_req(&B, BEEP_SHORT, 1000);
        run(&B, 1000, 2000, &h, &e);          /* 跑 2s，远超图案时长 */
        ck("响了 1 声", e, 1);
        ck("高电平拍数 = SHORT_MS", h, (long)BEEP_SHORT_MS);
        ck("2s 后已静音", beep_step(&B, 3000), 0);
        ck("busy = 0", beep_busy(&B), 0);
    }

    printf("-- 3. DOUBLE：两声，总长 = on+gap+on\n");
    {
        beep_t B; int h, e;
        beep_init(&B);
        beep_req(&B, BEEP_DOUBLE, 1000);
        run(&B, 1000, 2000, &h, &e);
        ck("响了 2 声", e, 2);
        ck("高电平拍数 = 2*SHORT_MS", h, (long)(2u * BEEP_SHORT_MS));
        ck("2s 后已静音", beep_step(&B, 3000), 0);
    }

    printf("-- 4. LONG：一声，长度 = BEEP_LONG_MS\n");
    {
        beep_t B; int h, e;
        beep_init(&B);
        beep_req(&B, BEEP_LONG, 1000);
        run(&B, 1000, 2000, &h, &e);
        ck("响了 1 声", e, 1);
        ck("高电平拍数 = LONG_MS", h, (long)BEEP_LONG_MS);
    }

    printf("-- 5. on 相位内必须**持续**为高（不是只在到期那一拍）\n");
    {
        beep_t B;
        beep_init(&B);
        beep_req(&B, BEEP_SHORT, 1000);
        ck("t+0 高", beep_step(&B, 1000), 1);
        ck("t+1 高", beep_step(&B, 1001), 1);
        ck("t+SHORT-1 高", beep_step(&B, 1000 + BEEP_SHORT_MS - 1), 1);
        ck("t+SHORT 转低", beep_step(&B, 1000 + BEEP_SHORT_MS), 0);
    }

    printf("-- 6. 覆盖式：新请求打断旧图案（ABORT 长鸣要能盖掉 DONE 双响）\n");
    {
        beep_t B; int h, e;
        beep_init(&B);
        beep_req(&B, BEEP_DOUBLE, 1000);
        beep_step(&B, 1010);
        beep_req(&B, BEEP_LONG, 1010);          /* 双响还没放完就改成长鸣 */
        run(&B, 1010, 2000, &h, &e);
        ck("只响 1 声(长鸣)", e, 1);
        ck("长度 = LONG_MS", h, (long)BEEP_LONG_MS);
    }

    printf("-- 7. BEEP_NONE 请求 = 立即静音\n");
    {
        beep_t B;
        beep_init(&B);
        beep_req(&B, BEEP_LONG, 1000);
        ck("正在响", beep_step(&B, 1010), 1);
        beep_req(&B, BEEP_NONE, 1010);
        ck("立即静音", beep_step(&B, 1011), 0);
        ck("busy = 0", beep_busy(&B), 0);
    }

    printf("-- 8. 32 位毫秒回绕跨 0 不许乱\n");
    {
        beep_t B; int h, e;
        uint32_t t0 = 0xFFFFFFF0u;              /* 距回绕 16ms */
        beep_init(&B);
        beep_req(&B, BEEP_SHORT, t0);
        run(&B, t0, 2000, &h, &e);              /* 期间会跨过 0 */
        ck("仍只响 1 声", e, 1);
        ck("长度仍 = SHORT_MS", h, (long)BEEP_SHORT_MS);
    }

    printf("-- 9. 稀疏调用（主循环 10ms 一拍）也不能把图案吃掉\n");
    {
        beep_t B; int e = 0, prev = 0; uint32_t i;
        beep_init(&B);
        beep_req(&B, BEEP_DOUBLE, 1000);
        for (i = 0; i < 200; i++) {              /* 10ms 步长，跑 2s */
            int lv = beep_step(&B, 1000 + i * 10u);
            if (lv && !prev) e++;
            prev = lv;
        }
        ck("10ms 步长下仍响 2 声", e, 2);
        ck("结束后静音", beep_step(&B, 5000), 0);
    }

    printf("\n  passed=%d  failed=%d\n", g_ok, g_bad);
    printf("%s\n", g_bad == 0 ? "==== ALL PASS ====" : "==== FAIL ====");
    return g_bad ? 1 : 0;
}
