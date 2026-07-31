/*
 * test_ball.c - 车载滚球平衡控制层(ball.c) 的 PC 单元测试（2026-H）
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -Wextra -I.. -o test_ball.exe test_ball.c ../ball.c -lm
 *   .\test_ball.exe
 * 或直接跑 _run_test_ball.ps1（会自动找 gcc）。
 *
 * ── 这份测试存在的理由 ──
 *   相机和摆杆机构都还没有，所以它是球控制**目前唯一能拿到的证据**。它验的是与硬件无关的
 *   那一半：动力学系数、三个前馈的解析正确性、观测器在稀疏测量下的行为、几个安全门、
 *   以及"闭环到底收不收敛"。留给真机的只剩两件：**两个装配符号**（S1/S2 实验）与**增益整定**。
 *
 * ── 里面内置了一个被控对象仿真 ──
 *   x'' = K_BALL·sin(theta_b + pitch) - (5/7)·a_x
 *   它就是 ball.h 推导的那个方程本身。⚠ 因此"仿真通过"**不能**证明模型对（同源），
 *   它证明的是**控制律在该模型下的行为符合我们的定量预期**（前馈能不能约掉扰动、
 *   稳态误差是不是等于解析值、闭环收不收敛）。模型本身要靠真机 M3b 那个"给 2° 量球走 5cm
 *   用时"的实验去证（理论 0.64s）。这条边界必须说清，否则就是自己骗自己。
 */
#include "ball.h"
#include <stdio.h>
#include <math.h>

#define DEG2RADf 0.017453292519943295f

static int g_fail = 0;

static void ck_true(const char *name, int cond)
{
    if (cond) printf("  [PASS] %-52s\n", name);
    else    { printf("  [FAIL] %-52s\n", name); g_fail++; }
}
static void ck_near(const char *name, float got, float want, float tol)
{
    float d = got - want; if (d < 0) d = -d;
    if (d <= tol) printf("  [PASS] %-52s got=%.4f want=%.4f tol=%.4f\n", name, got, want, tol);
    else        { printf("  [FAIL] %-52s got=%.4f want=%.4f tol=%.4f\n", name, got, want, tol); g_fail++; }
}
static void ck_int(const char *name, long got, long want)
{
    if (got == want) printf("  [PASS] %-52s got=%ld want=%ld\n", name, got, want);
    else           { printf("  [FAIL] %-52s got=%ld want=%ld\n", name, got, want); g_fail++; }
}

/* ================= 被控对象仿真（就是 ball.h 那个方程）================= */
typedef struct { float x, v; } plant_t;

static void plant_step(plant_t *p, float theta_b_deg, float pitch_deg, float ax, float dt)
{
    float th_g = (theta_b_deg + pitch_deg) * DEG2RADf;
    float a = BALL_K_MM_S2_PER_RAD * sinf(th_g) - BALL_ROLL_COEF * ax;
    p->x += p->v * dt + 0.5f * a * dt * dt;
    p->v += a * dt;
}

/* 固件里滚球回路的真实频率 = 1000/CFG_BALL_MS = 1000/20 = 50Hz。
 * 凡是**数字会写进设计报告**的那几组必须用它，否则报告里的"由固件 ball.c 导出"
 * 就名不副实（曾经这里写 100Hz，与固件的 20ms 节拍不符）。
 * 例外只有 test_observer_sparse：那组刻意用更快的 100Hz 制造更稀疏的测量、当上界测。 */
#define SIM_CTRL_HZ   50.0f
#define SIM_CAM_FPS   30.0f

/*
 * 闭环仿真一段时间。
 *   ctrl_hz  = 控制回路频率（报告相关的组一律传 SIM_CTRL_HZ）
 *   cam_fps  = 相机帧率（30 ⇒ 约三分之一的拍没有新测量，这是真实工况）
 *   noise_mm = 加在相机读数上的确定性锯齿噪声幅值（不用 rand()，保证可复现）
 * 返回最后一拍的状态。
 */
static ball_state_t sim_run(ball_t *b, plant_t *p, float seconds,
                           float ax, float pitch_deg,
                           float ctrl_hz, float cam_fps, float noise_mm)
{
    float dt = 1.0f / ctrl_hz;
    float cam_dt = (cam_fps > 0.0f) ? (1.0f / cam_fps) : 0.0f;
    float t_cam = cam_dt;          /* 让第一拍就出一帧，否则观测器 est_init 起不来 */
    float age = 0.0f;
    float th = 0.0f;
    int   n = (int)(seconds * ctrl_hz + 0.5f);
    int   i, k = 0;
    ball_state_t st = BALL_IDLE;

    for (i = 0; i < n; i++) {
        ball_in_t in;
        int have = 0;

        t_cam += dt;
        if (cam_dt <= 0.0f || t_cam >= cam_dt) { t_cam = 0.0f; have = 1; }

        if (have) age = 0.0f; else age += dt;

        in.x_mm       = p->x + (noise_mm > 0.0f ? ((k++ & 1) ? noise_mm : -noise_mm) : 0.0f);
        in.meas_valid = have;
        in.meas_age_s = age;
        in.ax_mm_s2   = ax;
        in.pitch_deg  = pitch_deg;
        in.dt_s       = dt;

        st = ball_step(b, &in, &th);
        plant_step(p, th, pitch_deg, ax, dt);
    }
    return st;
}

/* ============ 1. 轨迹剖面：端点精确、段末速度为 0、二次积分自洽 ============ */
static void test_traj_profile(void)
{
    ball_t b; float x, v, a;
    printf("test_traj_profile:\n");
    ball_init(&b);

    ball_traj_sample(&b, 0.0f, &x, &v, &a);
    ck_near("t=0 位置=0", x, 0.0f, 1e-3f);
    ck_near("t=0 速度=0", v, 0.0f, 1e-3f);

    /* 段1 末（t=t_out）应精确到 +amp 且速度归零 */
    ball_traj_sample(&b, b.traj_t_out, &x, &v, &a);
    ck_near("段1末 位置=+50mm", x, 50.0f, 0.05f);
    ck_near("段1末 速度=0", v, 0.0f, 0.5f);

    /* 停留段 */
    ball_traj_sample(&b, b.traj_t_out + 0.5f * b.traj_t_dwell, &x, &v, &a);
    ck_near("停留段 位置保持 +50mm", x, 50.0f, 1e-3f);
    ck_near("停留段 加速度=0", a, 0.0f, 1e-3f);

    /* 段3 末应到 -amp */
    ball_traj_sample(&b, b.traj_t_out + b.traj_t_dwell + b.traj_t_back, &x, &v, &a);
    ck_near("段3末 位置=-50mm", x, -50.0f, 0.1f);
    ck_near("段3末 速度=0", v, 0.0f, 0.5f);

    /* 结束后保持末端并返回 0 */
    ck_int("超出总时长 -> 返回 0(结束)",
           ball_traj_sample(&b, ball_traj_duration(&b) + 1.0f, &x, &v, &a), 0);
    ck_near("结束后位置保持 -50mm", x, -50.0f, 1e-3f);

    /* 把 a_ref 数值二次积分，和 x_ref 对表 —— 这一条直接验"前馈用的加速度和轨迹是同一条" */
    {
        float dt = 1e-4f, t, xi = 0.0f, vi = 0.0f, worst = 0.0f;
        float T = ball_traj_duration(&b);
        for (t = 0.0f; t < T; t += dt) {
            float xr, vr, ar, d;
            ball_traj_sample(&b, t, &xr, &vr, &ar);
            xi += vi * dt + 0.5f * ar * dt * dt;
            vi += ar * dt;
            d = xi - xr; if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        ck_true("a_ref 二次积分 == x_ref (最大偏差 < 0.5mm)", worst < 0.5f);
        printf("        (积分-参考 最大偏差 = %.4f mm)\n", worst);
    }
}

/* ============ 2. 时间与摆角预算：默认参数必须落在题目/机械极限之内 ============ */
static void test_traj_budget(void)
{
    ball_t b; float t, x, v, a, peak_a = 0.0f, peak_th;
    printf("test_traj_budget (要求3 的 5s + 机械极限 11.54 度):\n");
    ball_init(&b);

    ck_true("轨迹总时长 <= 5s (题目要求3)", ball_traj_duration(&b) <= 5.0f);
    printf("        (总时长 = %.2f s)\n", ball_traj_duration(&b));

    for (t = 0.0f; t < ball_traj_duration(&b); t += 0.005f) {
        ball_traj_sample(&b, t, &x, &v, &a);
        if (fabsf(a) > peak_a) peak_a = fabsf(a);
    }
    peak_th = (peak_a / BALL_K_MM_S2_PER_RAD) * 57.29577951f;
    ck_true("轨迹前馈峰值摆角 <= 2 度 (审题算的 1.14 度)", peak_th <= 2.0f);
    printf("        (峰值 |a_ref| = %.1f mm/s^2 -> %.2f 度, 占机械极限 %.0f%%)\n",
           peak_a, peak_th, 100.0f * peak_th / BALL_THETA_MECH_MAX_DEG);

    ck_true("默认限幅 <= 机械极限", b.theta_max_deg <= BALL_THETA_MECH_MAX_DEG);
}

/* ============ 3. 三个安全门 ============ */
static void test_safety_gates(void)
{
    ball_t b; ball_in_t in; float th = 99.0f;
    printf("test_safety_gates:\n");

    /* 3a 配置非法 -> BLOCKED 且输出 0（不许拿 0 增益假装在控制） */
    ball_init(&b); b.kp = 0.0f;
    ball_set_hold(&b, 0.0f);
    in.x_mm = 10; in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 0;
    in.pitch_deg = 0; in.dt_s = 0.01f;
    ck_int("kp=0 -> BLOCKED", ball_step(&b, &in, &th), BALL_BLOCKED);
    ck_near("BLOCKED 时输出 0 度", th, 0.0f, 1e-6f);
    ck_true("fail == BADCFG", b.fail == BALL_F_BADCFG);

    /* 3b 从没收到测量 -> BLOCKED（不许拿 0 当"球在中心"） */
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.meas_valid = 0; in.meas_age_s = 0.0f; th = 99.0f;
    ck_int("从未收到测量 -> BLOCKED", ball_step(&b, &in, &th), BALL_BLOCKED);
    ck_true("fail == NO_MEAS", b.fail == BALL_F_NO_MEAS);

    /* 3c 测量超龄 -> BLOCKED 且**摊平**，不是沿用上一个角度 */
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = 40.0f; in.meas_valid = 1; in.meas_age_s = 0.0f;
    ball_step(&b, &in, &th);
    ck_true("先建立测量后输出非 0 (球偏 40mm)", fabsf(th) > 0.5f);
    {
        float th_prev = th;
        in.meas_valid = 0; in.meas_age_s = b.max_age_s + 0.01f;
        ck_int("测量超龄 -> BLOCKED", ball_step(&b, &in, &th), BALL_BLOCKED);
        ck_near("超龄时摊平 (0 度)", th, 0.0f, 1e-6f);
        ck_true("且不是沿用上一个角度", fabsf(th_prev) > 0.5f);
    }
}

/* ============ 4. PD 符号：球偏 +x 必须往 -x 推（符号反了就是发散） ============ */
static void test_pd_sign(void)
{
    ball_t b; ball_in_t in; float th = 0.0f;
    printf("test_pd_sign (符号反了现象是'PID 怎么都调不出来'):\n");

    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = 30.0f; in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 0;
    in.pitch_deg = 0; in.dt_s = 0.01f;
    ball_step(&b, &in, &th);
    ck_true("球在 +30mm -> 输出负角 (把球推回 -x)", th < 0.0f);

    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = -30.0f;
    ball_step(&b, &in, &th);
    ck_true("球在 -30mm -> 输出正角 (把球推回 +x)", th > 0.0f);

    /* 幅值对表：e=50mm、v=0 -> theta = kp*e/K = 9*50/7007 rad = 3.68 度 */
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = 50.0f;
    ball_step(&b, &in, &th);
    ck_near("e=50mm 时 |theta| = 3.68 度 (解析值)", fabsf(th), 3.68f, 0.15f);
}

/* ============ 5. ⭐ a_x 前馈的分母必须是 g 不是 K_BALL ============ */
static void test_ax_ff_uses_g_not_k(void)
{
    ball_t b; ball_in_t in; float th = 0.0f;
    float want_g = (300.0f / BALL_G_MM_S2) * 57.29577951f;            /* 正解 1.7522 度 */
    float wrong_k = (300.0f / BALL_K_MM_S2_PER_RAD) * 57.29577951f;   /* 错解 2.4531 度 */
    printf("test_ax_ff_uses_g_not_k (写错会过补偿 40%%, 现象隐蔽):\n");

    ball_init(&b); ball_set_hold(&b, 0.0f);
    /* 球正好在目标上 => PD 项为 0 => 输出里只剩 a_x 前馈，可单独读出来 */
    in.x_mm = 0.0f; in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 300.0f;
    in.pitch_deg = 0; in.dt_s = 0.01f;
    ball_step(&b, &in, &th);

    ck_near("a_x=300mm/s^2 -> th_ax = 1.752 度 (= a_x/g)", b.th_ax_deg, want_g, 0.01f);
    ck_true("且不等于 a_x/K_BALL 的错解 2.453 度",
            fabsf(b.th_ax_deg - wrong_k) > 0.5f);
    ck_near("PD 项为 0 (球在目标上)", b.th_pd_deg, 0.0f, 1e-3f);
}

/* ============ 6. pitch 补偿：符号与幅值 ============ */
static void test_pitch_ff(void)
{
    ball_t b; ball_in_t in; float th = 0.0f;
    printf("test_pitch_ff:\n");
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = 0.0f; in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 0;
    in.pitch_deg = 0.5f; in.dt_s = 0.01f;
    ball_step(&b, &in, &th);
    ck_near("pitch=+0.5 度 -> th_pitch = -0.5 度 (1:1 反向抵消)", b.th_pitch_deg, -0.5f, 1e-4f);

    b.ff_pitch_en = 0;
    ball_step(&b, &in, &th);
    ck_near("关掉开关后 th_pitch = 0", b.th_pitch_deg, 0.0f, 1e-6f);
}

/* ============ 7. ⭐⭐ 仿真：前馈到底值多少毫米（稳态误差对解析值） ============ */
static void test_ff_cancels_in_sim(void)
{
    printf("test_ff_cancels_in_sim (这一条给出报告要的 A/B 数字):\n");

    /* 7a 恒定车加速度 a_x = 300 mm/s^2（= 我们设计的限幅值） */
    {
        float want_off = BALL_ROLL_COEF * 300.0f / 9.0f;   /* 解析: (5/7)*ax/kp = 23.81mm */
        ball_t b; plant_t p = { 0.0f, 0.0f };
        ball_init(&b); b.ff_ax_en = 0; ball_set_hold(&b, 0.0f);
        sim_run(&b, &p, 4.0f, 300.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
        ck_near("a_x 前馈[关] 稳态误差 = -(5/7)ax/kp = -23.8mm", b.err_mm, -want_off, 2.5f);

        {
            ball_t b2; plant_t p2 = { 0.0f, 0.0f };
            ball_init(&b2); b2.ff_ax_en = 1; ball_set_hold(&b2, 0.0f);
            sim_run(&b2, &p2, 4.0f, 300.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
            ck_true("a_x 前馈[开] 稳态误差 < 1mm", fabsf(b2.err_mm) < 1.0f);
            printf("        (关=%.2fmm  开=%.3fmm  => 前馈值 %.1f mm)\n",
                   b.err_mm, b2.err_mm, fabsf(b.err_mm) - fabsf(b2.err_mm));
        }
    }

    /* 7b 恒定车体俯仰 pitch = 0.5 度（审题里那个"吃掉 68% 预算"的量） */
    {
        float want_off = BALL_K_MM_S2_PER_RAD * sinf(0.5f * DEG2RADf) / 9.0f; /* 6.79mm */
        ball_t b; plant_t p = { 0.0f, 0.0f };
        ball_init(&b); b.ff_pitch_en = 0; ball_set_hold(&b, 0.0f);
        sim_run(&b, &p, 4.0f, 0.0f, 0.5f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
        ck_near("pitch 补偿[关] 稳态误差 = K*sin(0.5deg)/kp = +6.8mm", b.err_mm, want_off, 1.0f);

        {
            ball_t b2; plant_t p2 = { 0.0f, 0.0f };
            ball_init(&b2); b2.ff_pitch_en = 1; ball_set_hold(&b2, 0.0f);
            sim_run(&b2, &p2, 4.0f, 0.0f, 0.5f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
            ck_true("pitch 补偿[开] 稳态误差 < 1mm", fabsf(b2.err_mm) < 1.0f);
            printf("        (关=%.2fmm  开=%.3fmm  => 补偿值 %.1f mm)\n",
                   b.err_mm, b2.err_mm, fabsf(b.err_mm) - fabsf(b2.err_mm));
        }
    }
}

/* ============ 8. 观测器：30fps 稀疏测量 + 噪声下还能跟得住 ============ */
static void test_observer_sparse(void)
{
    ball_t b; plant_t p = { 0.0f, 0.0f };
    /* 这里刻意用 100Hz 而不是固件的 50Hz(CFG_BALL_MS=20ms): 控制拍越快、相机帧率不变
     * ⇒ 无测量的拍占比越高(100Hz 下 2/3, 50Hz 下 1/3) ⇒ 对观测器**更苛刻**。
     * 也就是说这一组是上界测试, 真机工况比它宽松。别把 100Hz 当成固件节拍。 */
    printf("test_observer_sparse (相机 30fps, 控制 100Hz => 2/3 的拍无测量; 比固件 50Hz 更苛刻):\n");
    ball_init(&b);
    ball_set_hold(&b, 40.0f);          /* 让球动起来，考察动态跟踪而不是静态 */
    sim_run(&b, &p, 3.0f, 0.0f, 0.0f, 100.0f, SIM_CAM_FPS, 0.3f);   /* ±0.3mm 锯齿噪声；100Hz 见上注 */

    ck_true("估计位置与真值差 < 1.5mm", fabsf(b.x_est - p.x) < 1.5f);
    ck_true("估计速度与真值差 < 20mm/s", fabsf(b.v_est - p.v) < 20.0f);
    ck_true("没有新测量的拍数 > 总拍数一半 (确认真的在稀疏工况)",
            b.no_meas_ticks > 150);
    printf("        (x_est=%.2f 真值=%.2f | v_est=%.1f 真值=%.1f | 无测量拍=%lu)\n",
           b.x_est, p.x, b.v_est, p.v, b.no_meas_ticks);
}

/* ============ 9. 软限位：越界主动回中 + warn 不许消失 ============ */
static void test_soft_limit(void)
{
    ball_t b; ball_in_t in; float th = 0.0f;
    printf("test_soft_limit:\n");
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = b.x_soft_mm + 10.0f;    /* 球已越界（+120mm）*/
    in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 0; in.pitch_deg = 0; in.dt_s = 0.01f;
    ball_step(&b, &in, &th);
    ck_true("球越软限位 -> 目标被拉到中心", fabsf(b.x_ref_mm) < 1e-3f);
    ck_true("输出把球往 -x 推", th < 0.0f);
    ck_true("warn == LIMIT", b.warn == BALL_F_LIMIT);

    /* warn 必须是 sticky：球回到安全区后仍要能报"这趟贴过限位" */
    in.x_mm = 0.0f;
    ball_step(&b, &in, &th);
    ck_true("球回安全区后 warn 仍在 (sticky, 别在成功那刻消失)", b.warn == BALL_F_LIMIT);

    /* setpoint 本身也不许被设到限位外 */
    ball_set_hold(&b, 300.0f);
    ck_near("setpoint 被夹在软限位内", b.setpoint_mm, b.x_soft_mm, 1e-3f);
}

/* ============ 10. 倾角限幅 ============ */
static void test_theta_clamp(void)
{
    ball_t b; ball_in_t in; float th = 0.0f;
    printf("test_theta_clamp:\n");
    ball_init(&b); ball_set_hold(&b, 0.0f);
    in.x_mm = 100.0f;   /* 巨大偏差 -> PD 索要 9*100/7007 rad = 7.4 度 > 限幅 6 度 */
    in.meas_valid = 1; in.meas_age_s = 0; in.ax_mm_s2 = 0; in.pitch_deg = 0; in.dt_s = 0.01f;
    ball_step(&b, &in, &th);
    ck_near("大偏差时输出被夹在 -6 度", th, -b.theta_max_deg, 1e-4f);
    ck_int("sat 标志置起 (撞限幅要能被看到)", b.sat, 1);

    /* 就算把限幅配成超过机械极限，也不许真的输出超过它 */
    ball_init(&b); b.theta_max_deg = 30.0f; ball_set_hold(&b, 0.0f);
    ball_step(&b, &in, &th);
    ck_true("配置 30 度也不会超过机械极限 11.54 度",
            fabsf(th) <= BALL_THETA_MECH_MAX_DEG + 1e-3f);
}

/* ============ 11. 闭环收敛：从 30mm 偏差回中 ============ */
static void test_closed_loop_settles(void)
{
    ball_t b; plant_t p = { 30.0f, 0.0f };
    float overshoot = 0.0f, worst_neg = 0.0f;
    int i;
    printf("test_closed_loop_settles (zeta=1.0 => 官方 Q37'考察全程' ⇒ 超调必须压到近零):\n");
    ball_init(&b); ball_set_hold(&b, 0.0f);

    /* 逐段跑，抓真正的超调峰值（只看末态会漏掉中途冲过头那一帧，
     * 而官方 Q37 明说"考察全程"⇒ 判分看的正是那一帧）*/
    for (i = 0; i < 20; i++) {
        sim_run(&b, &p, 0.1f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
        if (p.x < worst_neg) worst_neg = p.x;
    }
    overshoot = -worst_neg;
    ck_true("2s 内 |误差| < 3mm", fabsf(b.err_mm) < 3.0f);
    ck_true("全程超调 < 2mm (zeta=1.0 应近似无超调)", overshoot < 2.0f);
    printf("        (全程最大反向冲过量 = %.3f mm)\n", overshoot);
    printf("        (2s 后 x=%.2fmm v=%.1fmm/s peak_err=%.2fmm)\n",
           p.x, p.v, b.peak_abs_err_mm);

    /* 再跑 2s：必须继续收敛而不是振荡起来 */
    {
        float e_before = fabsf(b.err_mm);
        sim_run(&b, &p, 2.0f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
        ck_true("再跑 2s 误差没有变大 (无自激)", fabsf(b.err_mm) <= e_before + 0.5f);
    }
}

/* ============ 12. ⭐ 要求 3 全流程仿真：两个航点误差 + 总时长 ============ */
static void test_req3_full(void)
{
    ball_t b; plant_t p = { 0.0f, 0.0f };
    printf("test_req3_full (要求3: O->+5cm->折返->-5cm, <=5s, 误差<=1cm):\n");
    ball_init(&b);
    ball_start_traj(&b, 0.0f);            /* 用默认 ±50mm */
    sim_run(&b, &p, ball_traj_duration(&b) + 0.5f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.3f);

    ck_true("轨迹已跑完 (mode 自动转 HOLD)", b.traj_phase == 4);
    ck_true("总时长 <= 5s", b.traj_total_s <= 5.0f);
    ck_true("+5cm 航点最大误差 <= 10mm (题目门限)", b.err_wp_out_mm >= 0.0f && b.err_wp_out_mm <= 10.0f);
    ck_true("-5cm 终点误差 <= 10mm (题目门限)", b.err_wp_back_mm >= 0.0f && b.err_wp_back_mm <= 10.0f);
    /* 我们自己的目标是 ±3~5mm（判分取最坏帧，留 2~3 倍裕量），比门限严 */
    ck_true("+5cm 航点误差 <= 5mm (我们的自设目标)", b.err_wp_out_mm <= 5.0f);
    ck_true("-5cm 终点误差 <= 5mm (我们的自设目标)", b.err_wp_back_mm <= 5.0f);
    ck_true("全程未撞限幅 (说明摆角权限够用)", b.sat == 0);
    printf("        (总时长=%.2fs | +5cm误差=%.2fmm | -5cm误差=%.2fmm | 跟踪峰值误差=%.2fmm)\n",
           b.traj_total_s, b.err_wp_out_mm, b.err_wp_back_mm, b.peak_abs_err_mm);
}

/* ============ 13. 峰值误差必须记最坏值（判分取最坏帧） ============ */
static void test_peak_tracks_worst(void)
{
    ball_t b; plant_t p = { 25.0f, 0.0f };
    printf("test_peak_tracks_worst (判分靠回放视频, 取最坏帧不是平均):\n");
    ball_init(&b); ball_set_hold(&b, 0.0f);
    sim_run(&b, &p, 3.0f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
    ck_true("peak >= 初始偏差 20mm", b.peak_abs_err_mm >= 20.0f);
    ck_true("peak 明显大于末态误差 (证明记的是最坏不是当前)",
            b.peak_abs_err_mm > fabsf(b.err_mm) + 5.0f);
    printf("        (peak=%.2fmm  末态=%.2fmm)\n", b.peak_abs_err_mm, b.err_mm);

    ball_reset_stats(&b);
    ck_near("reset_stats 后 peak 归零", b.peak_abs_err_mm, 0.0f, 1e-6f);
    ck_true("reset_stats 后航点误差回到'未测到'(负数)", b.err_wp_out_mm < 0.0f);
}

/* ============ 14. 模型预测开关的对照（证明 use_model 真的有用） ============ */
static void test_use_model_helps(void)
{
    ball_t b1, b2; plant_t p1 = { 0.0f, 0.0f }, p2 = { 0.0f, 0.0f };
    float e1, e2;
    printf("test_use_model_helps (带模型预测 vs 纯线性外推):\n");

    ball_init(&b1); b1.use_model = 1; ball_set_hold(&b1, 60.0f);
    sim_run(&b1, &p1, 1.0f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
    e1 = fabsf(b1.v_est - p1.v);

    ball_init(&b2); b2.use_model = 0; ball_set_hold(&b2, 60.0f);
    sim_run(&b2, &p2, 1.0f, 0.0f, 0.0f, SIM_CTRL_HZ, SIM_CAM_FPS, 0.0f);
    e2 = fabsf(b2.v_est - p2.v);

    ck_true("带模型的速度估计误差 <= 不带模型的", e1 <= e2 + 1.0f);
    printf("        (速度估计误差: 带模型=%.2f  不带=%.2f mm/s)\n", e1, e2);
}

int main(void)
{
    printf("=== test_ball: 2026-H 车载滚球平衡控制层 ===\n");
    printf("K_BALL = %.2f mm/s^2 per rad  (= 5/7 * g)\n", BALL_K_MM_S2_PER_RAD);
    printf("机械极限 = %.2f 度 (题目 h>=5cm, 杆长 25cm) -> a_max = %.1f mm/s^2\n\n",
           BALL_THETA_MECH_MAX_DEG,
           BALL_K_MM_S2_PER_RAD * sinf(BALL_THETA_MECH_MAX_DEG * DEG2RADf));

    test_traj_profile();
    test_traj_budget();
    test_safety_gates();
    test_pd_sign();
    test_ax_ff_uses_g_not_k();
    test_pitch_ff();
    test_ff_cancels_in_sim();
    test_observer_sparse();
    test_soft_limit();
    test_theta_clamp();
    test_closed_loop_settles();
    test_req3_full();
    test_peak_tracks_worst();
    test_use_model_helps();

    printf("\n=== %s (%d fail) ===\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
