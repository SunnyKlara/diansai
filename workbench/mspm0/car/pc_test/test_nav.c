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
    printf("test_no_calibration (ENC_COUNTS_PER_MM=0 时走 mm 必须拒绝):\n");
    nav_t n; nav_init(&n);
    ck("counts_per_mm 默认未标定", n.counts_per_mm, 0.0f, 1e-6f);
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
    simcar_t s; sim_init(&s);
    nav_start_straight(&n, 1000.0f, 0, 0, 0.0f);
    int k = run_until_done(&n, &s, 1, 0.01, 30.0);

    ck_true("到位 (NAV_DONE)", n.state == NAV_DONE);
    ck("实际走了 ~1000mm", n.done_mm, 1000.0f, (float)n.tol_mm);
    ck_true("没超调过头(<=目标+容差)", n.done_mm <= 1000.0f + n.tol_mm);
    ck_true("用了合理拍数(不是首拍就宣布完成)", k > 10);
    printf("        [info] %d 拍完成, 峰值航向偏差 %.2f deg\n", k, n.peak_hdg_deg);
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
    printf("test_turn_deadband_sign (小误差也要给得动的指令, 且符号跟误差):\n");
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
    /* 这一条对应位置环精定位那个真机 bug: 前馈若按"PID 输出符号"叠, 末端过零会反复翻号 -> 狂震。
     * 按误差方向叠时, 只要没跨过目标, 符号就是恒定的。 */
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
    test_straight_heading_hold();
    test_straight_ramp();
    test_straight_reverse();
    test_straight_stall();
    test_straight_offcourse();
    test_turn_90();
    test_turn_settle();
    test_turn_deadband_sign();
    test_turn_encoder_fallback();
    test_idempotent();

    printf("\n==== %s ====\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    if (g_fail) printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
