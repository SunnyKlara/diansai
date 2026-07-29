/*
 * test_servo.c - 舵机角度<->脉宽映射的 PC 单测（不碰 HAL）
 *
 * 编译: gcc -O2 -Wall -Wextra -I.. -o test_servo.exe test_servo.c
 *   （servo.h 里那部分是 static inline 纯逻辑, 不需要链 servo.c —— servo.c 只有 HAL 那几行）
 *
 * 为什么值得单测这么"简单"的映射: 它错一次的代价是**把转向拉杆顶到机械限位 + 舵机堵转烧毁**,
 * 而真机上试错要先烧 112s 的固件。这里能提前抓的正是最容易写错的三类:
 *   ① 两级限幅的顺序/漏掉一级（角度夹住了但脉宽没夹, 或反过来）
 *   ② sign=-1 时的方向与反算是否自洽（deg->us->deg 往返）
 *   ③ us=0（limp）这个特例被当成"角度值"去夹到 min_us —— 那会让"停脉冲"变成"满舵"
 *
 * 断言全部盯**契约**, 不依赖 config.h 里那些 ⬜未标定 的占位数值:
 * 每个用例自带一组标定值, 所以以后回填真实中位/极限时本测试不会假失败。
 */
#include <stdio.h>
#include <math.h>
#include "servo.h"

static int g_fail = 0, g_total = 0;

static void ck(int cond, const char *what)
{
    g_total++;
    if (!cond) { g_fail++; printf("  FAIL: %s\n", what); }
}

static void ck_int(int got, int want, const char *what)
{
    g_total++;
    if (got != want) { g_fail++; printf("  FAIL: %s (got %d, want %d)\n", what, got, want); }
}

static void ck_near(float got, float want, float tol, const char *what)
{
    g_total++;
    if (fabsf(got - want) > tol) {
        g_fail++; printf("  FAIL: %s (got %.3f, want %.3f +-%.3f)\n", what, got, want, tol);
    }
}

/* 一组"宽松"标定: 硬限幅刻意开得比角度行程大, 这样角度级限幅是唯一起作用的那级 */
static servo_cal_t cal_wide(void)
{
    servo_cal_t c;
    c.center_us = 1500; c.min_us = 500; c.max_us = 2500;
    c.us_per_deg = 10.0f; c.max_deg = 30.0f; c.sign = 1;
    return c;
}

int main(void)
{
    printf("== test_servo ==\n");

    /* ---- 1. 中位与线性 ---- */
    {
        servo_cal_t c = cal_wide();
        ck_int(servo_us_from_deg(&c, 0.0f),   1500, "0 度 = 中位");
        ck_int(servo_us_from_deg(&c, 10.0f),  1600, "+10 度 = 中位+100us");
        ck_int(servo_us_from_deg(&c, -10.0f), 1400, "-10 度 = 中位-100us");
        /* 四舍五入(不是截断): 0.55 度 * 10us = 5.5us -> 1506 */
        ck_int(servo_us_from_deg(&c, 0.55f),  1506, "四舍五入而非截断");
    }

    /* ---- 2. 角度级限幅（第一级）---- */
    {
        servo_cal_t c = cal_wide();          /* max_deg=30 -> 1800us; 硬限幅 2500 够宽 */
        ck_int(servo_us_from_deg(&c,  99.0f), 1800, "超 max_deg 被夹到 +30 度对应脉宽");
        ck_int(servo_us_from_deg(&c, -99.0f), 1200, "超 -max_deg 被夹到 -30 度对应脉宽");
    }

    /* ---- 3. 脉宽级限幅（第二级, 硬保护）----
     * 这一级是"标定错了也不许顶机械限位"的最后一道墙: 角度还在 max_deg 以内,
     * 但 us_per_deg 偏大导致算出的脉宽越界时, 必须被夹住。 */
    {
        servo_cal_t c = cal_wide();
        c.min_us = 1400; c.max_us = 1600;    /* 硬限幅收得比角度行程窄 */
        ck_int(servo_us_from_deg(&c,  30.0f), 1600, "角度合法但脉宽越界 -> 夹到 max_us");
        ck_int(servo_us_from_deg(&c, -30.0f), 1400, "角度合法但脉宽越界 -> 夹到 min_us");
        /* 两级都生效时结果应等于"先夹角度再夹脉宽", 不能因为顺序写反而放过越界值 */
        ck(servo_us_from_deg(&c, 999.0f) <= c.max_us, "任何输入都不得超过 max_us");
        ck(servo_us_from_deg(&c, -999.0f) >= c.min_us, "任何输入都不得低于 min_us");
    }

    /* ---- 4. sign = -1 时方向反转, 且 deg->us->deg 往返自洽 ---- */
    {
        servo_cal_t c = cal_wide();
        c.sign = -1;
        ck_int(servo_us_from_deg(&c, 10.0f), 1400, "sign=-1: +10 度 = 中位-100us");
        ck_near(servo_deg_from_us(&c, 1400), 10.0f, 0.01f, "sign=-1 往返自洽");
        c.sign = 1;
        ck_near(servo_deg_from_us(&c, 1600), 10.0f, 0.01f, "sign=+1 往返自洽");
        ck_near(servo_deg_from_us(&c, 1500),  0.0f, 0.01f, "中位反算 = 0 度");
    }

    /* ---- 5. us=0 是 limp 特例, 绝不能被夹成 min_us ----
     * 写错的后果很具体: "停脉冲"变成"输出 min_us" = 满舵打死, 而人以为舵机松开了。 */
    {
        servo_cal_t c = cal_wide();
        ck_int(servo_us_clamp(&c, 0), 0, "us=0(limp) 原样放过, 不夹到 min_us");
        ck_int((int)servo_ticks_from_us(0), 0, "us=0 -> CC=0(不出脉冲)");
        ck_near(servo_deg_from_us(&c, 0), 0.0f, 0.01f, "limp 时角度按 0 报");
    }

    /* ---- 6. us -> CC 计数 ---- */
    {
        ck_int((int)servo_ticks_from_us(1500), 1500 * SERVO_TICKS_PER_US, "1500us -> 48000 counts");
        ck_int((int)servo_ticks_from_us(20000), SERVO_PWM_PERIOD, "脉宽 >= 周期时被夹到周期");
        ck((int)servo_ticks_from_us(1500) < SERVO_PWM_PERIOD, "中位脉宽必须远小于周期");
        /* 50Hz 的物理自证: 周期 tick 数 / 每 us 的 tick 数 = 20000us = 20ms */
        ck_int(SERVO_PWM_PERIOD / SERVO_TICKS_PER_US, 20000, "周期 = 20000us = 20ms = 50Hz");
    }

    /* ---- 7. 标定自洽性检查（拒绝机制）----
     * 显式构造矛盾值, 而不是依赖 config.h 里的占位数 —— 后者会随真机标定改变。 */
    {
        servo_cal_t c = cal_wide();
        ck(servo_cal_sane(&c), "宽松标定应判自洽");

        servo_cal_t b = cal_wide(); b.center_us = 3000;   /* 中位在硬限幅之外 */
        ck(!servo_cal_sane(&b), "中位超出硬限幅 -> 判不自洽");

        b = cal_wide(); b.max_us = b.min_us;              /* 区间退化 */
        ck(!servo_cal_sane(&b), "max_us<=min_us -> 判不自洽");

        b = cal_wide(); b.us_per_deg = 0.0f;              /* 会导致除零 */
        ck(!servo_cal_sane(&b), "us_per_deg=0 -> 判不自洽(否则反算除零)");

        b = cal_wide(); b.sign = 0;
        ck(!servo_cal_sane(&b), "sign 只能是 +-1");

        b = cal_wide(); b.max_deg = 0.0f;
        ck(!servo_cal_sane(&b), "max_deg=0 -> 判不自洽(转不了)");
    }

    /* ---- 8. config.h 当前默认值自洽（保护性: 防回填时手滑写出矛盾值）----
     * 只断言"自洽"和"中位在区间内", **不断言具体数值** —— 那些数还没真机标定。 */
    {
        servo_cal_t c;
        servo_cal_default(&c);
        ck(servo_cal_sane(&c), "config.h §7.9 默认标定必须自洽");
        ck(servo_us_from_deg(&c,  180.0f) <= c.max_us, "默认标定下极端角度仍在硬限幅内");
        ck(servo_us_from_deg(&c, -180.0f) >= c.min_us, "默认标定下极端角度仍在硬限幅内");
    }

    printf("%s: %d/%d checks passed\n", g_fail ? "SOME FAILED" : "ALL PASS",
           g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
