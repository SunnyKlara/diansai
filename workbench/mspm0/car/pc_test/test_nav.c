/*
 * test_nav.c - nav.c 车级导航层的 PC 单元测试（阶梯 2.5 / 3 / 4 落地前的唯一验证手段）
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -I.. -o test_nav test_nav.c ../nav.c ../attitude.c -lm
 *   ./test_nav          (Windows: .\test_nav.exe)
 *
 * 为什么值得写：车一落地，"走歪了"有至少五个可能原因（里程标定错 / 航向符号反 / 增益太软 /
 * 命令没到 / 机械不对称），现场分不开。这里把**与硬件无关的那几个**先钉死：
 *   符号约定、未标定的拒绝、梯形限速、到位判据、settle 防误报、卡住检测、编码器兜底。
 * 剩下留给真机的就只有"增益整定"和"机械"，排障空间小一个数量级。
 *
 * 每个测试都用一个**仿真车**推进：给定 (v,w) 指令 -> 假想左右轮转速 -> 累加计数 + 偏航角。
 * 仿真只需自洽，不需要物理精确 —— 它验的是控制逻辑的因果与符号，不是标定值。
 */
#include "nav.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;

static void ck(const char *name, float got, float want, float tol)
{
    float d = got - want; if (d < 0) d = -d;
    if (d <= tol) printf("  [PASS] %-34s got=%9.3f want=%9.3f (tol %.3f)\n", name, got, want, tol);
    else        { printf("  [FAIL] %-34s got=%9.3f want=%9.3f (tol %.3f)\n", name, got, want, tol); g_fail++; }
}
static void ck_int(const char *name, long got, long want)
{
    if (got == want) printf("  [PASS] %-34s got=%ld want=%ld\n", name, got, want);
    else           { printf("  [FAIL] %-34s got=%ld want=%ld\n", name, got, want); g_fail++; }
}
static void ck_true(const char *name, int cond)
{
    if (cond) printf("  [PASS] %-34s\n", name);
    else    { printf("  [FAIL] %-34s\n", name); g_fail++; }
}

/* ── 仿真车 ─────────────────────────────────────────────────────────
 * 左轮 RPM = v - w, 右轮 RPM = v + w（与 car.c 的 car_drive_mix 完全一致）。
 * 计数增量 = RPM/60 * ENC_CPR * dt。偏航角速度 ∝ (右 - 左)。
 * yaw_gain: 每 (RPM 差) 产生多少 dps —— 数值任取，只要前后一致；符号必须是"右快=左转=正"。
 */
typedef struct {
    double cl, cr;      /* 累计计数(浮点, 避免离散化掩盖逻辑问题) */
    double yaw;         /* 度 */
    double wz;          /* dps */
    double rpm_l, rpm_r;
    double yaw_gain;    /* dps per (RPM 差) */
    double slip;        /* 1.0=不打滑; <1 = 左右轮都打滑(里程虚高的反面, 这里只用来做 stall) */
    double bias_dps;    /* 外加的恒定扰动(模拟左右摩擦差导致的画弧), 度/秒 */
    int    stuck;       /* 1 = 轮子卡死: 给指令也不动 */
} simcar_t;

static void sim_init(simcar_t *s)
{
    s->cl = s->cr = 0.0; s->yaw = 0.0; s->wz = 0.0;
    s->rpm_l = s->rpm_r = 0.0;
    s->yaw_gain = 0.5; s->slip = 1.0; s->bias_dps = 0.0; s->stuck = 0;
}

static void sim_step(simcar_t *s, int v, int w, double dt)
{
    if (s->stuck) { s->rpm_l = s->rpm_r = 0.0; s->wz = 0.0; return; }
    /* 电机瞬间跟上指令(速度环已达标, 这里不重复仿真它) */
    s->rpm_l = (v - w) * s->slip;
    s->rpm_r = (v + w) * s->slip;
    s->cl += s->rpm_l / 60.0 * ENC_CPR * dt;
    s->cr += s->rpm_r / 60.0 * ENC_CPR * dt;
    s->wz  = (s->rpm_r - s->rpm_l) * s->yaw_gain + s->bias_dps;
    s->yaw += s->wz * dt;
}

static void sim_fill(const simcar_t *s, nav_in_t *in, double dt, int hdg_ok)
{
    in->counts_l   = (long)s->cl;
    in->counts_r   = (long)s->cr;
    in->yaw_deg    = (float)s->yaw;
    in->wz_dps     = (float)s->wz;
    in->rpm_avg    = (float)((s->rpm_l + s->rpm_r) / 2.0);
    in->dt_s       = (float)dt;
    in->heading_ok = hdg_ok;
}

/* 跑到结束或超时。返回拍数; 通过 out_v/out_w 回最后一次指令 */
static int run_until_done(nav_t *n, simcar_t *s, int hdg_ok, double dt, double tmax)
{
    nav_in_t in; int v = 0, w = 0, k = 0;
    int kmax = (int)(tmax / dt);
    for (k = 0; k < kmax; k++) {
        sim_fill(s, &in, dt, hdg_ok);
        if (nav_step(n, &in, &v, &w) != NAV_RUN) break;
        sim_step(s, v, w, dt);
    }
    return k;
}

/* ============ 1. 未标定必须拒绝，不许"当 1.0 用" ============ */
static void test_no_calibration(void)
{
    printf("test_no_calibration (counts_per_mm=0 时走 mm 必须拒绝):\n");
    nav_t n; nav_init(&n);
    /* ⚠ 2026-07-27 改: 原来这里断言 "counts_per_mm 默认就是 0"，即**依赖 config.h 的占位值**。
     * 里程标定落地后 ENC_COUNTS_PER_MM 变成 5.109，那条断言就必然挂 —— 而它想验的从来不是
     * "默认值是多少"，是"**未标定时拒绝行驶**"这个安全机制。所以显式把它置 0 再验机制本身，
     * 这样以后不管 config.h 填什么值，这个测试都还在守着它该守的东西。 */
    n.counts_per_mm = 0.0f;
    ck_true("nav_calibrated()==0", nav_calibrated(&n) == 0);

    nav_start_straight(&n, 500.0f, 0, 0, 0.0f);
    nav_in_t in = { 0, 0, 0.0f, 0.0f, 0.0f, 0.01f, 1 };
    int v = 999, w = 999;
    nav_state_t st = nav_step(&n, &in, &v, &w);
    ck_true("state == NAV_BLOCKED", st == NAV_BLOCKED);
    ck_true("fail == NAV_F_NO_CAL", n.fail == NAV_F_NO_CAL);
    ck_int("v 必须归零", v, 0);
    ck_int("w 必须归零", w, 0);
    /* 这一条是本测试的意义所在: 若未标定被"当 1.0 用", 走 500mm 就变成走 500counts(≈几 cm),
     * 现场表现是"电机没劲/走不到", 会把人引去查电机而不是查标定。 */
    ck("counts_to_mm 未标定返回 0", nav_counts_to_mm(&n, 12345), 0.0f, 1e-6f);
}

/* ============ 2. 走直: 走够距离就停, 且不超调 ============ */
static void test_straight_distance(void)
{
    printf("test_straight_distance (走 1000mm):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;          /* 假标定值: 12 counts/mm */
    /* ⚠ 仿真车**没有滑行物理**（指令归零就立刻停），所以"预留滑行余量"在仿真里只会表现为
     * 提前 coast_mm 停下。要验"距离精度"就必须先把补偿关掉，否则测的是补偿量而不是精度。
     * 补偿本身由 test_straight_coast 单独验（真机滑行数据见 config.h CFG_NAV_COAST_MM）。 */
    n.coast_mm = 0.0f;
    simcar_t s; sim_init(&s);
    nav_start_straight(&n, 1000.0f, 0, 0, 0.0f);
    int k = run_until_done(&n, &s, 1, 0.01, 30.0);

    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck("实际走了 ~1000mm", n.done_mm, 1000.0f, (float)n.tol_mm);
    ck_true("没超调过头(<=目标+容差)", n.done_mm <= 1000.0f + n.tol_mm);
    ck_true("用了合理拍数(不是首拍就宣布完成)", k > 10);
    printf("        [info] %d 拍完成, 峰值航向偏差 %.2f deg\n", k, n.peak_hdg_deg);
}

/* ============ 2b. 滑行余量: 停止点必须沿行进方向提前 coast_mm ============
 * 真机依据(2026-07-27 两趟 n300): PWM 归零后车还滑 16.4mm / 14.3mm，只差 2mm ⇒ 确定性可补偿。
 * 仿真里没有滑行，所以这里验的是"**补偿量是否被正确地、按方向地减掉**"——
 * 即同一目标下 coast=15 应该比 coast=0 少走约 15mm，倒车时同样少走 15mm(绝对值)。
 * 这条测试挡住两种最容易写错的实现: ① 符号搞反(倒车时反而多走) ② 用 fabs 导致方向无关。 */
static void test_straight_coast(void)
{
    printf("test_straight_coast (停止点 = 目标 - max(coast_mm, tol_mm), 且随行进方向):\n");
    /* ⚠ 别断言"coast=15 比 coast=0 少走 15mm" —— coast=0 时**本来就已经提前 tol_mm=10 停**
     * (到位容差的固有行为)，所以差值只有 15-10=5。直接断言契约本身更清楚也更难写错。 */
    const float tgt[2] = { 1000.0f, -1000.0f };
    const float cs[2]  = { 0.0f, 15.0f };
    for (int d = 0; d < 2; d++) {
        for (int c = 0; c < 2; c++) {
            nav_t n; nav_init(&n);
            n.counts_per_mm = 12.0f;
            n.coast_mm = cs[c];
            simcar_t s; sim_init(&s);
            nav_start_straight(&n, tgt[d], 0, 0, 0.0f);
            run_until_done(&n, &s, 1, 0.01, 30.0);
            float lead = (cs[c] > n.tol_mm) ? cs[c] : n.tol_mm;      /* 该提前多少收油 */
            float want = tgt[d] - ((tgt[d] >= 0.0f) ? lead : -lead); /* 提前量跟方向 */
            char name[112];
            snprintf(name, sizeof name, "tgt%+.0f coast=%.0f -> 停在目标前 %.0fmm",
                     (double)tgt[d], (double)cs[c], (double)lead);
            ck(name, n.done_mm, want, 3.0f);
            ck_true("符号没被 coast 弄反", (tgt[d] >= 0.0f) ? (n.done_mm > 0.0f) : (n.done_mm < 0.0f));
        }
    }
}

/* ============ 3. 走直: 有扰动时航向环必须把车拉回来(符号不能反) ============ */
static void test_straight_heading_hold(void)
{
    printf("test_straight_heading_hold (恒定扰动下航向必须被压住):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    simcar_t s; sim_init(&s);
    s.bias_dps = 8.0;                 /* 每秒往左偏 8 度: 模拟左右摩擦差/轮径差 */

    nav_start_straight(&n, 1000.0f, 0, 0, 0.0f);
    run_until_done(&n, &s, 1, 0.01, 30.0);
    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck_true("用的是陀螺纠偏(hdg_used==1)", n.hdg_used == 1);
    /* 关键断言: 有 8dps 的持续扰动, 若航向环符号反了, yaw 会被推到几十度以上。
     * 这里只要求"被压在 hdg_max 之内且远小于放任不管的量级"。 */
    printf("        [info] 峰值航向偏差 %.2f deg, 终点 yaw %.2f deg\n", n.peak_hdg_deg, s.yaw);
    ck_true("峰值航向偏差 < 10 deg (符号对且能压住)", n.peak_hdg_deg < 10.0f);

    /* 对照组: 同样的扰动, 关掉航向纠偏(heading_ok=0 且无编码器兜底) -> 必须明显更歪。
     * 有对照才证明"是航向环起的作用", 而不是仿真本身就不会歪。 */
    nav_t n2; nav_init(&n2); n2.counts_per_mm = 12.0f; n2.counts_per_deg = 0.0f;
    simcar_t s2; sim_init(&s2); s2.bias_dps = 8.0;
    nav_start_straight(&n2, 1000.0f, 0, 0, 0.0f);
    run_until_done(&n2, &s2, 0, 0.01, 30.0);
    printf("        [info] 对照(不纠偏) 终点 yaw %.2f deg\n", s2.yaw);
    ck_true("对照组明显更歪(证明是航向环的功劳)", fabs(s2.yaw) > 3.0 * n.peak_hdg_deg);
    /* ⚠ 这条断言曾经失败过, 抓出一个真设计缺陷: 告警原先塞在 fail 里, 而任务成功时
     * nav_finish 会把 fail 清成 NONE ⇒ "这趟没纠偏"的告警**正好在打成绩单那一刻消失**,
     * 一趟没纠偏的走直会被报成和"航向环压住了"完全一样。故拆出 warn 字段。 */
    ck_true("对照组任务仍然完成", n2.state == NAV_DONE);
    ck_true("对照组 warn==NO_HDG(成绩单里必须留着)", n2.warn == NAV_F_NO_HDG);
    ck_true("对照组 hdg_used==0", n2.hdg_used == 0);
    ck_true("有纠偏的那趟 warn 为空", n.warn == NAV_F_NONE);
}

/* ============ 4. 走直: 梯形斜坡不许一上来就全速 ============ */
static void test_straight_ramp(void)
{
    printf("test_straight_ramp (起步限速, 防打滑):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    n.accel_rpm_s = 200.0f; n.v_cruise = 150.0f;
    simcar_t s; sim_init(&s);
    nav_start_straight(&n, 5000.0f, 0, 0, 0.0f);

    nav_in_t in; int v = 0, w = 0;
    sim_fill(&s, &in, 0.01, 1);
    nav_step(&n, &in, &v, &w);
    /* 首拍 dt=10ms, accel=200RPM/s -> 斜坡只允许 2 RPM; 但 v_min=35 是下限 => 应等于 v_min */
    ck_int("首拍速度被限到 v_min", v, (long)n.v_min);
    ck_true("首拍远低于巡航速度", v < (int)n.v_cruise);

    for (int i = 0; i < 100; i++) { sim_step(&s, v, w, 0.01); sim_fill(&s, &in, 0.01, 1); nav_step(&n, &in, &v, &w); }
    ck_int("1s 后达到巡航速度", v, (long)n.v_cruise);
}

/* ============ 5. 走直: 倒车(负目标)符号要对 ============ */
static void test_straight_reverse(void)
{
    printf("test_straight_reverse (走 -600mm):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    n.coast_mm = 0.0f;                /* 同 test_straight_distance: 仿真无滑行, 验精度要先关补偿 */
    simcar_t s; sim_init(&s);
    nav_start_straight(&n, -600.0f, 0, 0, 0.0f);
    run_until_done(&n, &s, 1, 0.01, 30.0);
    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck("实际走了 ~-600mm", n.done_mm, -600.0f, (float)n.tol_mm);
    ck_true("计数确实是负的", s.cl < 0.0 && s.cr < 0.0);
}

/* ============ 6. 走直: 卡住要报 STALL, 不许闷头顶电机 ============ */
static void test_straight_stall(void)
{
    printf("test_straight_stall (撞墙/轮子卡死):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    simcar_t s; sim_init(&s);
    s.stuck = 1;                       /* 给指令也不动 */
    nav_start_straight(&n, 1000.0f, 0, 0, 0.0f);
    int k = run_until_done(&n, &s, 1, 0.01, 10.0);
    ck_true("state == NAV_BLOCKED", n.state == NAV_BLOCKED);
    ck_true("fail == NAV_F_STALL", n.fail == NAV_F_STALL);
    /* 时间上界: 必须在 stall_s 之后不久就判定, 不能一直顶着 */
    ck_true("在 stall_s 附近判定", k * 0.01 < n.stall_s + 0.2f);
    printf("        [info] %.2fs 后判 STALL (阈值 %.2fs)\n", k * 0.01, n.stall_s);
}

/* ============ 7. 走直: 偏太多要主动停(OFFCOURSE) ============ */
static void test_straight_offcourse(void)
{
    printf("test_straight_offcourse (航向偏出上限要停车):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    n.kp_hdg = 0.0f; n.kd_hdg = 0.0f;  /* 故意让航向环失效 -> 车会一路歪下去 */
    simcar_t s; sim_init(&s);
    s.bias_dps = 40.0;                 /* 强扰动 */
    nav_start_straight(&n, 5000.0f, 0, 0, 0.0f);
    run_until_done(&n, &s, 1, 0.01, 20.0);
    ck_true("state == NAV_BLOCKED", n.state == NAV_BLOCKED);
    ck_true("fail == NAV_F_OFFCOURSE", n.fail == NAV_F_OFFCOURSE);
    ck_true("停在上限附近, 没让它横着继续冲", fabs(s.yaw) < n.hdg_max_deg + 10.0f);
}

/* ============ 8. 原地转: 转 90 度 ============ */
static void test_turn_90(void)
{
    printf("test_turn_90 (原地左转 90 度):\n");
    nav_t n; nav_init(&n);
    simcar_t s; sim_init(&s);
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    int k = run_until_done(&n, &s, 1, 0.01, 20.0);
    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck("最终转角 ~90 deg", (float)s.yaw, 90.0f, (float)n.turn_tol_deg);
    ck_true("误差在容差内", fabsf(n.err_deg) <= n.turn_tol_deg);
    printf("        [info] %d 拍完成, 终点 %.2f deg, 残余误差 %.2f deg\n", k, s.yaw, n.err_deg);

    /* 右转对称性: 符号反了会在这里暴露(左转正/右转负是全工程约定) */
    nav_t n2; nav_init(&n2);
    simcar_t s2; sim_init(&s2);
    nav_start_turn(&n2, -90.0f, 0, 0, 0.0f);
    run_until_done(&n2, &s2, 1, 0.01, 20.0);
    ck_true("右转也到位", n2.state == NAV_DONE);
    ck("最终转角 ~-90 deg", (float)s2.yaw, -90.0f, (float)n2.turn_tol_deg);
}

/* ============ 9. 原地转: settle 判据必须挡住"正在飞过去" ============ */
static void test_turn_settle(void)
{
    printf("test_turn_settle (瞬时穿过目标不算到位):\n");
    nav_t n; nav_init(&n);
    n.turn_settle_s = 0.30f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);

    /* 手工构造: 角度正好等于目标, 但角速度很大(=正在高速穿过) -> 绝不许判 DONE */
    nav_in_t in = { 0, 0, 90.0f, 200.0f, 0.0f, 0.01f, 1 };
    int v = 0, w = 0;
    for (int i = 0; i < 50; i++) (void)nav_step(&n, &in, &v, &w);   /* 0.5s > settle */
    ck_true("高速穿过目标时不判 DONE", n.state == NAV_RUN);
    ck("settle 计时器保持 0", n.settle_t, 0.0f, 1e-6f);

    /* 现在停下来(角速度≈0): 保持 settle 时间后才算完成 */
    in.wz_dps = 0.0f;
    int done_at = -1;
    for (int i = 0; i < 100; i++) {
        if (nav_step(&n, &in, &v, &w) == NAV_DONE) { done_at = i; break; }
    }
    ck_true("停稳后才判 DONE", done_at >= 0);
    ck_true("等够了 settle 时间才判(约 30 拍)", done_at >= 29);
    printf("        [info] 第 %d 拍(=%.2fs)判 DONE, settle 阈值 %.2fs\n", done_at, done_at * 0.01, n.turn_settle_s);
}

/* ============ 10. 原地转: 死区前馈符号必须跟误差, 且末端不许翻号 ============ */
static void test_turn_deadband_sign(void)
{
    printf("test_turn_deadband_sign (起转补偿不许覆盖 D 制动):\n");
    nav_t n; nav_init(&n);
    n.kp_turn = 0.01f; n.kd_turn = 0.0f;     /* 故意让 P 算出来的指令小于死区 */
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);

    nav_in_t in = { 0, 0, 85.0f, 0.0f, 0.0f, 0.01f, 1 };   /* 还差 +5 度 */
    int v = 0, w = 0;
    nav_step(&n, &in, &v, &w);
    ck_int("正误差 -> 正指令(左转)", (w > 0) ? 1 : 0, 1);
    ck_int("指令被抬到死区前馈值", w, (long)n.turn_w_min);

    in.yaw_deg = 95.0f;                                    /* 冲过头 -5 度 */
    nav_step(&n, &in, &v, &w);
    ck_int("负误差 -> 负指令(右转回来)", (w < 0) ? 1 : 0, 1);
    ck_int("幅值同为死区前馈值", -w, (long)n.turn_w_min);

    /* 真机回放点: tgt=90, yaw=87.6, wz=+50, Kp=2.5, Kd=0.60 => raw w=-24。
     * 负号是 D 在目标前主动制动；旧逻辑因 |w|<30 把它强改为 +30，反而继续加速。 */
    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.60f; n.turn_w_min = 30.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 87.6f; in.wz_dps = 50.0f;
    nav_step(&n, &in, &v, &w);
    ck_true("左转目标前 D 反向制动不被改回正向", w < 0);
    ck_true("左转小制动力不被放大到死区下限", -w < (int)n.turn_w_min);

    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.60f; n.turn_w_min = 30.0f;
    nav_start_turn(&n, -90.0f, 0, 0, 0.0f);
    in.yaw_deg = -87.6f; in.wz_dps = -50.0f;
    nav_step(&n, &in, &v, &w);
    ck_true("右转目标前 D 反向制动不被改回负向", w > 0);
    ck_true("右转小制动力不被放大到死区下限", w < (int)n.turn_w_min);
}

/* ============ 10b. 起转下限的回差: 容差边缘不许再踹 ============
 * 真机来源(2026-07-29 方形链式测试, 逐拍 f50 抓到):
 *   err 仅 0.7 度时 PD 只要 w=-1, 旧逻辑却因 |err|>tol(2度) 把它抬到 w_min=55 (PWM 30%),
 *   车被踹出容差 -> err 2.9 -> 4.4 度, 往回修又被踹 => settle 反复重置, 转角耗时
 *   2.9/9.8/22.5s 随机, 最坏撞 15s 硬上限被强停(脚本侧 = TIMEOUT / 无 [nav] 成绩单)。
 *   单独跑 j90 不暴露它(从 90 度误差起跑能量大, 常一次冲进容差就 settle 完)。
 * 断言的是那条"只在离目标还远时才抬下限"的规则本身, 不是某个具体数值。 */
static void test_turn_ff_hysteresis(void)
{
    printf("test_turn_ff_hysteresis (容差边缘不许被抬到起转下限):\n");
    nav_t n; nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    int v = 0, w = 0;

    /* (a) 真机那一拍: 差 0.7 度、几乎不转 => PD 输出很小, 且必须**保持**很小 */
    nav_in_t in = { 0, 0, 89.3f, 0.5f, 0.0f, 0.02f, 1 };
    nav_step(&n, &in, &v, &w);
    ck_true("容差内(0.7deg)不被抬到 w_min", abs(w) < (int)n.turn_w_min);
    ck_true("容差内指令保持小量(<=Kp*err 量级)", abs(w) <= 5);

    /* (b) 刚出容差但仍在回差带内(3 度 < tol*2=4) 且**车正在转** => 仍不抬, 交给 PD。
     *     ⚠ 2026-07-29 翻案说明: 本条原来不带 "车正在转" 这个前提, 断言的是"回差带内一律不抬"。
     *     真机方形第 4 个转角把它推翻了 —— 车停在 err=2.6°(正落在这个带里), PD 只给
     *     2.5*2.6≈6.5RPM, 起不动静止的车, 700ms 后判 FAIL=STALL(done 87.3°)。
     *     ⇒ 现在的规则是"**运动中**不抬(避免恒幅振荡), **卡住了**才抬(破静摩擦)"，
     *     两个前提各自有真机证据, 所以这里按 rate 拆成 (b) 和 (b2) 两条。 */
    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 87.0f; in.wz_dps = 20.0f;         /* err = +3 度, 车正在转(20dps) */
    nav_step(&n, &in, &v, &w);
    ck_true("回差带内且车正在转 -> 不抬到 w_min", w < (int)n.turn_w_min);
    ck_true("回差带内方向依然正确(左转)", w > 0);
    /* (b2) 同样的 3 度, 但车**卡住不动** => 必须抬到 w_min 破静摩擦, 否则就是那次 STALL */
    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 87.0f; in.wz_dps = 0.0f;          /* err = +3 度, 车静止 */
    nav_step(&n, &in, &v, &w);
    ck_int("回差带内但车卡住 -> 抬到 w_min(破静摩擦)", w, (long)n.turn_w_min);
    /* (b3) 真机那一次的原始数值回放: err=+2.6 度 静止 => 必须给一脚, 不许坐等 STALL */
    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 87.4f; in.wz_dps = 0.0f;          /* err = +2.6 度 = 方形 leg4 的 STALL 点 */
    nav_step(&n, &in, &v, &w);
    ck_int("方形 leg4 的 STALL 点(2.6deg 静止) 现在会被踹一脚", w, (long)n.turn_w_min);

    /* (c) 明显离目标还远(20 度)且 PD 输出仍小 => 必须抬, 否则回到 STALL 老病 */
    nav_init(&n);
    n.kp_turn = 0.01f; n.kd_turn = 0.0f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 70.0f; in.wz_dps = 0.0f;          /* err = +20 度, Kp 故意极小 */
    nav_step(&n, &in, &v, &w);
    ck_int("远离目标时仍抬到 w_min(卡死治法没被弄坏)", w, (long)n.turn_w_min);

    /* (d) 反向对称: 冲过头 0.7 度也不许被踹回去 */
    nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    in.yaw_deg = 90.7f; in.wz_dps = 0.0f;          /* err = -0.7 度 */
    nav_step(&n, &in, &v, &w);
    ck_true("过冲 0.7deg 也不被抬到 w_min", abs(w) < (int)n.turn_w_min);
}
/* ============ 10b. 容差内指令必须**恰好 0**(躲开速度环的静摩擦补偿) ============
 * 为什么 10 的 "abs(w)<=5" 不够: 速度环的 breakaway 补偿(CFG_DRV_BREAKAWAY_*)看的是
 * "目标非 0 而轮子没转", 只要 w=±1 它就给 30% 占空。2026-07-29 真机逐拍: `w=2 -> PWM=-30,+30`,
 * 一脚踹出 |wz|=25~35dps, 而到位判据的角速度闸门只有 NAV_STALL_DPS*3=9dps ⇒ settle_t 反复清零
 * ⇒ 转到 15s 硬上限被强停、nav_finish 从未执行 ⇒ 连 [nav] 成绩单都不打(方形测试 leg1 turn
 * 21.2s TIMEOUT 而 dYaw 显示已到位 89.9°)。⇒ 容差内必须**一点指令都不给**, 差 1RPM 都不行。 */
static void test_turn_deadband_exact_zero(void)
{
    printf("test_turn_deadband_exact_zero (容差内指令必须恰好 0):\n");
    nav_t n; nav_init(&n);
    n.kp_turn = 2.5f; n.kd_turn = 0.25f; n.turn_w_min = 55.0f; n.turn_tol_deg = 2.0f;
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    int v = 0, w = 0;
    nav_in_t in = { 0, 0, 89.3f, 0.5f, 0.0f, 0.02f, 1 };
    /* (a) 容差内: 恰好 0, 不是"很小" */
    nav_step(&n, &in, &v, &w);
    ck_int("err=+0.7deg -> w 恰好 0", w, 0);
    /* (b) 容差内 + 车还在快转: 也不给制动指令(制动指令同样会被 breakaway 放大成 30%);
     *     靠 20:1 减速比与摩擦停车, 真机实测 |wz| 25.4->1.1dps 只用一拍 50ms */
    in.yaw_deg = 90.5f; in.wz_dps = 30.0f;
    nav_step(&n, &in, &v, &w);
    ck_int("容差内即使 |wz|=30dps 也不给制动指令", w, 0);
    /* (c) 容差边界外必须立刻有指令 —— 死区与作用区严格互补, 不许出现"两边都不管"的夹缝
     *     (那会让车停在 2~4 度不动, 又是一种永不 DONE) */
    in.yaw_deg = 87.5f; in.wz_dps = 0.0f;          /* err = +2.5 度, 出容差但在回差带内 */
    nav_step(&n, &in, &v, &w);
    ck_true("err=+2.5deg(刚出容差) 必须有非零指令", w != 0);
    ck_true("且方向正确(左转为正)", w > 0);
    in.yaw_deg = 92.5f;                            /* err = -2.5 度, 反向对称 */
    nav_step(&n, &in, &v, &w);
    ck_true("err=-2.5deg 反向也必须有非零指令", w != 0);
    ck_true("且方向正确(右转为负)", w < 0);
    /* (d) 死区没有把"能到位"弄坏: 整趟仿真仍须 DONE */
    nav_t n2; nav_init(&n2);
    simcar_t s; sim_init(&s);
    nav_start_turn(&n2, 90.0f, 0, 0, 0.0f);
    run_until_done(&n2, &s, 1, 0.01, 20.0);
    ck_true("加死区后整趟仍到位 (NAV_DONE)", n2.state == NAV_DONE);
    ck("终点仍在容差内", (float)s.yaw, 90.0f, 3.0f);
    /* (e) STALL 检测必须**命令感知**: 死区内我们故意给 0, 车当然不转, 不许把这算成卡住。
     *     原代码只看 rate ⇒ stall_t 照样累加, 与注释写的"给了指令但车不转"语义不符。 */
    nav_t n3; nav_init(&n3);
    n3.kp_turn = 2.5f; n3.kd_turn = 0.25f; n3.turn_w_min = 55.0f; n3.turn_tol_deg = 2.0f;
    nav_start_turn(&n3, 90.0f, 0, 0, 0.0f);
    nav_in_t q = { 0, 0, 89.5f, 0.0f, 0.0f, 0.02f, 1 };   /* err=+0.5 度, 车静止 */
    nav_step(&n3, &q, &v, &w);
    ck_int("死区内指令为 0", w, 0);
    ck("死区内不许累加 stall_t(指令为 0 就不算卡住)", n3.stall_t, 0.0f, 1e-6f);
    /* (f) 真·卡住仍必须抓到: 出容差 + 不转 => 指令被抬到 ±w_min(非 0) => stall_t 照常累加 */
    nav_t n4; nav_init(&n4);
    n4.kp_turn = 2.5f; n4.kd_turn = 0.25f; n4.turn_w_min = 55.0f; n4.turn_tol_deg = 2.0f;
    n4.stall_s = 0.10f;
    nav_start_turn(&n4, 90.0f, 0, 0, 0.0f);
    nav_in_t r = { 0, 0, 80.0f, 0.0f, 0.0f, 0.02f, 1 };   /* err=+10 度, 车死活不转 */
    nav_state_t st = NAV_RUN;
    for (int i = 0; i < 20 && st == NAV_RUN; i++) st = nav_step(&n4, &r, &v, &w);
    ck_true("真卡住(出容差+不转) 仍判 STALL", n4.fail == NAV_F_STALL);
}

/* ============ 11. 编码器兜底: 陀螺不可用时仍能转 ============ */
static void test_turn_encoder_fallback(void)
{
    printf("test_turn_encoder_fallback (陀螺没标定, 走编码器兜底):\n");
    nav_t n; nav_init(&n);
    /* 兜底标定值必须与仿真自洽: 仿真里 (dR-dL) 计数与 yaw 的关系 =
     *   yaw = (rpm_r-rpm_l)*yaw_gain*dt,  (dR-dL) = (rpm_r-rpm_l)/60*ENC_CPR*dt
     *   => counts_per_deg = ENC_CPR/60/yaw_gain */
    n.counts_per_deg = (float)(ENC_CPR / 60.0 / 0.5);
    simcar_t s; sim_init(&s);
    nav_start_turn(&n, 90.0f, 0, 0, 0.0f);
    run_until_done(&n, &s, 0 /* heading_ok=0 */, 0.01, 20.0);
    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck_true("用的是编码器兜底(hdg_used==2)", n.hdg_used == 2);
    ck("真实转角 ~90 deg", (float)s.yaw, 90.0f, 5.0f);   /* 兜底精度差些, 容差放宽 */
    printf("        [info] 兜底终点 %.2f deg (真值), 估计值 %.2f deg\n", s.yaw, n.done_deg);

    /* 两个兜底都没有 -> 必须明确拒绝, 不许瞎转 */
    nav_t n3; nav_init(&n3); n3.counts_per_deg = 0.0f;
    nav_start_turn(&n3, 90.0f, 0, 0, 0.0f);
    nav_in_t in = { 0, 0, 0.0f, 0.0f, 0.0f, 0.01f, 0 };
    int v = 1, w = 1;
    ck_true("无角度来源 -> BLOCKED", nav_step(&n3, &in, &v, &w) == NAV_BLOCKED);
    ck_true("fail == NAV_F_NO_HDG", n3.fail == NAV_F_NO_HDG);
    ck_int("指令归零", v + w, 0);
}

/* ============ 12. 幂等性: DONE/BLOCKED 之后不许自己复活 ============ */
static void test_idempotent(void)
{
    printf("test_idempotent (到位后再调用不许重新起跑):\n");
    nav_t n; nav_init(&n);
    n.counts_per_mm = 12.0f;
    simcar_t s; sim_init(&s);
    nav_start_straight(&n, 300.0f, 0, 0, 0.0f);
    run_until_done(&n, &s, 1, 0.01, 20.0);
    ck_true("先到位", n.state == NAV_DONE);

    nav_in_t in; int v = 7, w = 7;
    sim_fill(&s, &in, 0.01, 1);
    for (int i = 0; i < 10; i++) {
        nav_state_t st = nav_step(&n, &in, &v, &w);
        if (st != NAV_DONE || v != 0 || w != 0) { ck_true("DONE 后持续回 0", 0); return; }
    }
    ck_true("DONE 后持续回 0", 1);

    nav_abort(&n);
    ck_true("abort 后回 IDLE", n.state == NAV_IDLE);
    ck_true("IDLE 下 nav_step 回 IDLE 且指令为 0",
            nav_step(&n, &in, &v, &w) == NAV_IDLE && v == 0 && w == 0);
}

int main(void)
{
    printf("==== test_nav (nav.c 车级导航层) ====\n");
    printf("ENC_CPR=%.1f  ENC_COUNTS_PER_MM=%.4f  ENC_COUNTS_PER_DEG=%.4f\n\n",
           (double)ENC_CPR, (double)ENC_COUNTS_PER_MM, (double)ENC_COUNTS_PER_DEG);
    test_no_calibration();
    test_straight_distance();
    test_straight_coast();
    test_straight_heading_hold();
    test_straight_ramp();
    test_straight_reverse();
    test_straight_stall();
    test_straight_offcourse();
    test_turn_90();
    test_turn_settle();
    test_turn_deadband_sign();
    test_turn_ff_hysteresis();
    test_turn_deadband_exact_zero();
    test_turn_encoder_fallback();
    test_idempotent();

    printf("\n==== %s ====\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    if (g_fail) printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
