/*
 * ball.c - 车载滚球平衡控制层实现（2026-H）
 *
 * 设计说明与全部数值依据见 ball.h 文件头；本文件只写"为什么这么实现"的实现级注释。
 * 纯算法层：只 include ball.h（它只 include <math.h>）⇒ 可 PC 单测。
 *
 * 状态: 2026-07-29 新建。PC 单测 pc_test/test_ball.c；**真机零验证**。
 */
#include "ball.h"

#define DEG2RAD  0.017453292519943295f
#define RAD2DEG  57.29577951308232f

/* ── 小工具 ─────────────────────────────────────────────────────── */
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * 单段轨迹：三角形加速度剖面（前半段 +a、后半段 -a）。
 *   段内位移 S、时长 T ⇒ a = 4S/T^2，段末速度精确为 0。
 * 为什么用它而不是"给阶跃让 PD 自己冲"：
 *   阶跃 5cm 会让 PD 索要 kp*50/K = 3.7° 并冲过头；而规划成 1.2s 走完只需 |a|=139mm/s^2
 *   ⇒ 前馈角仅 1.14° ⇒ 摆角权限（机械极限 11.54°）只用掉 10%，PD 只修残差。
 * 为什么不用 S 曲线（jerk 限幅）：
 *   S 曲线的 a_ref 是分段线性、推导与单测都更啰嗦，而本系统的执行器带宽富余 50 倍
 *   （静态负载余量 14~50 倍、动态力矩比静态小 50 倍），不存在需要限 jerk 的理由。
 */
static void seg_eval(float t, float T, float x0, float x1,
                     float *x, float *v, float *a)
{
    float S, acc, half, tau;

    if (T <= 0.0f) { *x = x1; *v = 0.0f; *a = 0.0f; return; }
    if (t < 0.0f) t = 0.0f;
    if (t > T)    t = T;

    S    = x1 - x0;
    acc  = 4.0f * S / (T * T);
    half = 0.5f * T;

    if (t <= half) {
        *a = acc;
        *v = acc * t;
        *x = x0 + 0.5f * acc * t * t;
    } else {
        tau = t - half;
        *a = -acc;
        *v = acc * half - acc * tau;
        *x = x0 + 0.5f * acc * half * half + acc * half * tau - 0.5f * acc * tau * tau;
    }
}

/* ── 初始化 ─────────────────────────────────────────────────────── */
void ball_init(ball_t *b)
{
    if (!b) return;

    /* 控制器 [纸面]：ωn = 3 rad/s ⇒ kp = ωn^2 = 9
     * ⭐ ζ 由 0.8 提到 **1.0**（kd = 2ζωn = 6.0）—— 依据是 2026-07-29 官方答疑：
     *   Q37「钢球运行全程瞬时误差是否都不能超过 1cm？…短暂振荡超差后快速回正是否直接判失败」
     *      → 官方答：**"考察全程"** ⇒ 任何一帧超差都算，短暂振荡不被豁免。
     *   Q50「钢球脱落如何判定」→ **"钢球脱落即判定本次失败"**。
     *   ⇒ 超调成了**纯风险、零收益**：这里没有"上升时间"类指标要抢（要求3 限 5s 而我们
     *     的轨迹只用 4.5s、只吃掉 10% 摆角权限），所以把阻尼加到临界是**免费的保险**。
     * ⚠ 别拿 4/(ζωn) 说"ζ=1.0 还更快" —— 那个公式只对欠阻尼成立，ζ≥1 时不适用。
     *   PC 仿真(50Hz, 30mm 阶跃, 只改 kd)实测: ζ=1.0 反向过冲 0.000mm / 5% 调节 1.56s;
     *   ζ=0.8 过冲 -0.43mm / 调节 1.10s。**真实取舍 = 用 0.46s 调节时间买掉 0.43mm 过冲**,
     *   而那 0.46s 落在轨迹已有的 0.5s 余量内(4.5s vs 限时 5s) ⇒ 仍然选 ζ=1.0。
     *   复现: pc_test/dump_ball_sim.c 的 sim_step30.csv 与 sim_step30_z08.csv。
     * 校验（ball.h 有推导）：e=50mm 时 θ = 9*50/7007 rad = 3.68° < 6° ⇒ 不饱和 ✓
     * ⚠ 采样率不能拿 ωn=0.48Hz 去比（那样算出 30fps 是 60 倍过采样，是错的）：
     *   判据是**回路增益穿越频率** ωc=0.985Hz 的 20 倍 = 19.7Hz ⇒ 30fps 只有 1.52 倍余量，
     *   控制回路 50Hz 是 2.54 倍。推导见报告 2.3.3。 */
    b->kp = 9.0f;
    b->kd = 6.0f;

    /* Weak integral is compiled in but disabled by default. The real-board sequence is: prove the
     * unchanged PD baseline, then change only Ki online. The band and angle cap bound the experiment. */
    b->ki = 0.0f;
    b->i_band_mm = 20.0f;
    b->i_limit_deg = 0.30f;

    /* 摩擦前馈默认**关闭** —— 它的正确幅值是"实测起动阈值"，而那个值随管子清洁度/位置变化很大
     * (2026-07-31 实测 0.53~1.59°，3 倍散布) ⇒ 不该有一个纸面默认值。用 `J<x100>` 在线整定后回填。 */
    b->fric_deg = 0.0f;

    /* 限幅 [保护]：机械极限 11.54°（题目 h>=5cm 推出），取 6° 留一倍余量。
     * 6° 对应球加速度 733mm/s^2，而我们把车的加速度限在 300mm/s^2（前馈只需 1.75°）
     * ⇒ 剩下的角度权限全部留给 PD 与 pitch 补偿。 */
    b->theta_max_deg = 6.0f;

    /* 软/硬限位 [题目]：刻度到 ±12cm、管端（挡片）在 ±12.5cm ⇒ 软限位 ±11cm 留 1cm 缓冲 */
    b->x_soft_mm = 110.0f;
    b->x_hard_mm = 125.0f;

    /* 观测器 [纸面]：α-β 的"临界阻尼"关系 β = α^2/(2-α)，α=0.5 ⇒ β=0.1667。
     * 带模型预测后可以把 α 取小一点（更信模型、更抗测量噪声），赛场按噪声实测再调。 */
    b->alpha = 0.5f;
    b->beta  = 0.1667f;
    b->use_model = 1;

    /* 测量超龄门限 [保护]：30fps ⇒ 正常帧间 33ms。取 150ms ≈ 掉 4 帧才判不可信，
     * 既能容忍偶发掉帧，又远小于"球从静止被 6° 推出 10mm 所需的 165ms"⇒ 不会等到球跑飞。 */
    b->max_age_s = 0.15f;

    /* 前馈两个都默认开 —— 校赛B E1 最痛的教训：解药做出来了却没在场上启用。 */
    b->ff_ax_en    = 1;
    b->ff_pitch_en = 1;

    /* 要求 3 轨迹 [题目+纸面]：题目要 ±50mm、总时 <=5s。
     * 1.2 + 0.3 + 1.8 + 1.2 = 4.5s，留 0.5s 余量。
     * 峰值前馈角：段1 |a|=4*50/1.2^2=139mm/s^2 ⇒ 1.14°；段3 |a|=4*100/1.8^2=123 ⇒ 1.01° */
    b->traj_amp_mm   = 50.0f;
    b->traj_t_out    = 1.2f;
    b->traj_t_dwell  = 0.3f;
    b->traj_t_back   = 1.8f;
    b->traj_t_settle = 1.2f;

    b->mode  = BALL_M_OFF;
    b->state = BALL_IDLE;
    b->fail  = BALL_F_NONE;
    b->warn  = BALL_F_NONE;
    b->setpoint_mm = 0.0f;

    b->x_est = 0.0f;
    b->v_est = 0.0f;
    b->est_init = 0;
    b->t_since_meas = 0.0f;
    b->no_meas_ticks = 0;

    b->traj_t = 0.0f;
    b->traj_phase = 0;

    b->theta_cmd_deg = 0.0f;
    b->th_pd_deg = b->th_traj_deg = b->th_ax_deg = b->th_pitch_deg = b->th_fric_deg = 0.0f;
    b->sat = 0;
    ball_reset_integral(b);

    b->x_ref_mm = b->v_ref_mm_s = b->a_ref_mm_s2 = 0.0f;

    ball_reset_stats(b);
}

void ball_reset_stats(ball_t *b)
{
    if (!b) return;
    b->err_mm = 0.0f;
    b->peak_abs_err_mm = 0.0f;
    /* 航点误差用负数表示"还没测到"，避免把"没跑过"报成"误差 0mm 完美" */
    b->err_wp_out_mm  = -1.0f;
    b->err_wp_back_mm = -1.0f;
    b->traj_total_s = 0.0f;
    b->no_meas_ticks = 0;
    b->warn = BALL_F_NONE;
}

void ball_reset_integral(ball_t *b)
{
    if (!b) return;
    b->i_err_mm_s = 0.0f;
    b->th_i_deg = 0.0f;
    b->i_active = 0;
    b->i_aw_hold = 0;
}

void ball_set_hold(ball_t *b, float setpoint_mm)
{
    if (!b) return;
    if (b->x_soft_mm > 0.0f)
        setpoint_mm = clampf(setpoint_mm, -b->x_soft_mm, b->x_soft_mm);
    b->mode = BALL_M_HOLD;
    b->state = BALL_HOLD;
    b->setpoint_mm = setpoint_mm;
    b->fail = BALL_F_NONE;
    b->warn = BALL_F_NONE;
    b->traj_phase = 0;
    b->traj_t = 0.0f;
    ball_reset_integral(b);
}

void ball_start_traj(ball_t *b, float amp_mm)
{
    if (!b) return;
    if (amp_mm > 0.0f) b->traj_amp_mm = amp_mm;
    b->mode = BALL_M_TRAJ;
    b->state = BALL_TRAJ_RUN;
    b->fail = BALL_F_NONE;
    b->warn = BALL_F_NONE;
    b->traj_t = 0.0f;
    b->traj_phase = 0;
    b->setpoint_mm = 0.0f;
    ball_reset_integral(b);
    ball_reset_stats(b);
}

void ball_abort(ball_t *b)
{
    if (!b) return;
    b->mode = BALL_M_OFF;
    b->state = BALL_IDLE;
    b->traj_phase = 0;
    b->traj_t = 0.0f;
    b->theta_cmd_deg = 0.0f;
    b->th_pd_deg = b->th_traj_deg = b->th_ax_deg = b->th_pitch_deg = b->th_fric_deg = 0.0f;
    b->sat = 0;
    ball_reset_integral(b);
}

/* ── 轨迹采样（纯 t 的函数）───────────────────────────────────────── */
float ball_traj_duration(const ball_t *b)
{
    if (!b) return 0.0f;
    return b->traj_t_out + b->traj_t_dwell + b->traj_t_back + b->traj_t_settle;
}

int ball_traj_sample(const ball_t *b, float t, float *x, float *v, float *a)
{
    float amp, T1, T2, T3, T4;
    float xx = 0.0f, vv = 0.0f, aa = 0.0f;
    int running = 1;

    if (!b) return 0;
    amp = b->traj_amp_mm;
    T1 = b->traj_t_out;
    T2 = b->traj_t_dwell;
    T3 = b->traj_t_back;
    T4 = b->traj_t_settle;

    if (t < 0.0f) t = 0.0f;

    if (t < T1) {                             /* 段1: O -> +amp */
        seg_eval(t, T1, 0.0f, amp, &xx, &vv, &aa);
    } else if (t < T1 + T2) {                 /* 段2: 在 +amp 停留（"到达后折返"看得清）*/
        xx = amp;
    } else if (t < T1 + T2 + T3) {            /* 段3: +amp -> -amp */
        seg_eval(t - (T1 + T2), T3, amp, -amp, &xx, &vv, &aa);
    } else if (t < T1 + T2 + T3 + T4) {       /* 段4: 在 -amp 稳定 */
        xx = -amp;
    } else {                                  /* 结束：保持末端 */
        xx = -amp;
        running = 0;
    }

    if (x) *x = xx;
    if (v) *v = vv;
    if (a) *a = aa;
    return running;
}

/* ── 走一拍 ─────────────────────────────────────────────────────── */
ball_state_t ball_step(ball_t *b, const ball_in_t *in, float *theta_deg_out)
{
    float dt, a_known, th_g_rad, meas_dt = 0.0f;
    float x_ref = 0.0f, v_ref = 0.0f, a_ref = 0.0f;
    float e, ev, a_pd, th, th_base, lim, ae;
    int   running, i_reset_this_step = 0;

    if (!b || !in) { if (theta_deg_out) *theta_deg_out = 0.0f; return BALL_IDLE; }

    dt = in->dt_s;
    if (dt <= 0.0f) dt = 1e-3f;    /* 防 0 除。真实 dt 必须由调用方给（见 ball.h 警示）*/

    /* ── 0. 配置合法性：宁可响亮失败，也别拿 0 增益假装在控制 ──────────── */
    if (b->kp <= 0.0f || b->theta_max_deg <= 0.0f || b->ki < 0.0f ||
        (b->ki > 0.0f && (b->i_band_mm <= 0.0f || b->i_limit_deg <= 0.0f))) {
        b->fail  = BALL_F_BADCFG;
        b->state = BALL_BLOCKED;
        b->theta_cmd_deg = 0.0f;
        b->th_pd_deg = b->th_traj_deg = b->th_ax_deg = b->th_pitch_deg = b->th_fric_deg = 0.0f;
        b->sat = 0;
        ball_reset_integral(b);
        if (theta_deg_out) *theta_deg_out = 0.0f;
        return BALL_BLOCKED;
    }

    /* ── 1. 观测器：先按模型预测，再用相机测量纠正 ───────────────────
     * 预测里代入的是**上一拍实际输出的倾角**（含限幅后的值）+ 本拍 pitch。
     * pitch 无条件参与 —— 它是物理，不是我们的选择；ff_pitch_en 只决定"要不要补偿它"。 */
    b->t_since_meas += dt;

    a_known = 0.0f;
    if (b->use_model) {
        th_g_rad = (b->theta_cmd_deg + in->pitch_deg) * DEG2RAD;
        a_known  = BALL_K_MM_S2_PER_RAD * sinf(th_g_rad)
                 - BALL_ROLL_COEF * in->ax_mm_s2;
    }
    b->x_est += b->v_est * dt + 0.5f * a_known * dt * dt;
    b->v_est += a_known * dt;

    if (in->meas_valid) {
        float ti = b->t_since_meas;
        if (ti < 1e-4f) ti = 1e-4f;
        if (!b->est_init) {
            /* 第一个测量直接当真值落座，速度设 0（没有第二点算不出速度）*/
            b->x_est = in->x_mm;
            b->v_est = 0.0f;
            b->est_init = 1;
        } else {
            float r = in->x_mm - b->x_est;
            b->x_est += b->alpha * r;
            b->v_est += (b->beta / ti) * r;   /* ⚠ 除测量间隔，不是控制拍长 */
            /* Integrate only on a real camera interval. A first frame has no interval; a frame after a
             * gap longer than the feedback freshness bound must re-establish control without adding a
             * large lump to I. This keeps the effective Ki independent of camera FPS. */
            if (ti <= b->max_age_s) meas_dt = ti;
        }
        b->t_since_meas = 0.0f;
    } else {
        b->no_meas_ticks++;
    }

    /* ── 2. 反馈可信门（"先证反馈可信，再碰控制器" —— 超声波时代的血泪）─────
     * 摊平摆杆而不是沿用上一个角度：陈旧倾角会持续给球加速度，球一路加速撞挡片。 */
    if (!b->est_init || in->meas_age_s > b->max_age_s) {
        b->fail  = BALL_F_NO_MEAS;
        b->state = BALL_BLOCKED;
        b->theta_cmd_deg = 0.0f;
        b->th_pd_deg = b->th_traj_deg = b->th_ax_deg = b->th_pitch_deg = b->th_fric_deg = 0.0f;
        b->sat = 0;
        ball_reset_integral(b);
        if (theta_deg_out) *theta_deg_out = 0.0f;
        return BALL_BLOCKED;
    }
    b->fail = BALL_F_NONE;

    if (b->mode == BALL_M_OFF) {
        b->state = BALL_IDLE;
        b->theta_cmd_deg = 0.0f;
        b->th_pd_deg = b->th_traj_deg = b->th_ax_deg = b->th_pitch_deg = b->th_fric_deg = 0.0f;
        b->sat = 0;
        ball_reset_integral(b);
        if (theta_deg_out) *theta_deg_out = 0.0f;
        return BALL_IDLE;
    }

    /* ── 3. 目标：HOLD 用 setpoint；TRAJ 由轨迹给 ────────────────────── */
    if (b->mode == BALL_M_TRAJ) {
        float T1 = b->traj_t_out;
        float T2 = T1 + b->traj_t_dwell;
        float T3 = T2 + b->traj_t_back;
        float T4 = T3 + b->traj_t_settle;
        int phase_before = b->traj_phase;

        b->traj_t += dt;
        running = ball_traj_sample(b, b->traj_t, &x_ref, &v_ref, &a_ref);
        b->setpoint_mm = x_ref;

        /* 航点误差抓取（要求 3 的两个考核点）。
         * err_wp_out  = 在 +amp 停留期间的**最大**偏差（含冲过头后回落，读法偏严）
         * err_wp_back = 稳定段末尾那一刻的偏差（"稳定在该点附近"的读法）
         * ⚠ 用"最大"而非"某一瞬间"是因为题目原文写的是"±5cm 处的**最大**误差绝对值"，
         *   而且判分靠回放视频逐帧看 ⇒ 任何一帧超差都算。 */
        if (b->traj_t >= T1 && b->traj_t < T2) {
            float d = b->x_est - b->traj_amp_mm;
            if (d < 0.0f) d = -d;
            if (b->err_wp_out_mm < 0.0f || d > b->err_wp_out_mm) b->err_wp_out_mm = d;
            b->traj_phase = 1;
        } else if (b->traj_t >= T2 && b->traj_t < T3) {
            b->traj_phase = 2;
        } else if (b->traj_t >= T3 && b->traj_t < T4) {
            b->traj_phase = 3;
        }

        if (!running) {
            float d = b->x_est - (-b->traj_amp_mm);
            if (d < 0.0f) d = -d;
            b->err_wp_back_mm = d;
            if (b->traj_total_s <= 0.0f) b->traj_total_s = b->traj_t;
            b->traj_phase = 4;
            /* 跑完自动转 HOLD，继续把球稳在 -amp（松手就滚走，不能停止控制）。
             * 本拍仍然报 TRAJ_DONE，下一拍起报 HOLD ⇒ 上层能捕到"刚跑完"这一次事件。 */
            b->mode = BALL_M_HOLD;
            b->setpoint_mm = -b->traj_amp_mm;
            b->state = BALL_TRAJ_DONE;
        } else {
            b->state = BALL_TRAJ_RUN;
        }

        /* A return, settle, or completed phase has a different target/direction. Do not carry a
         * bias learned in the previous phase across that discontinuity. Phase 0->1 is continuous
         * at +amp, so it intentionally keeps I; phases 2/3/4 clear it. */
        if (b->traj_phase != phase_before && b->traj_phase >= 2) {
            ball_reset_integral(b);
            i_reset_this_step = 1;
        }
    } else {
        x_ref = b->setpoint_mm;
        v_ref = 0.0f;
        a_ref = 0.0f;
        b->state = BALL_HOLD;
    }

    /* ── 4. 软限位：目标夹住 + 越界主动回中 ─────────────────────────── */
    if (b->x_soft_mm > 0.0f) {
        x_ref = clampf(x_ref, -b->x_soft_mm, b->x_soft_mm);
        if (b->x_est > b->x_soft_mm || b->x_est < -b->x_soft_mm) {
            x_ref = 0.0f;      /* 球已越界 ⇒ 目标拉到中心，主动把它拽回来 */
            v_ref = 0.0f;
            a_ref = 0.0f;
            b->warn = BALL_F_LIMIT;   /* sticky：留到成绩单，别在成功那一刻消失 */
            /* Limit recovery is a new safety objective. Historical bias from the old target can
             * push farther into the end stop, so discard it and suppress reintegration this frame. */
            ball_reset_integral(b);
            i_reset_this_step = 1;
        }
    }

    b->x_ref_mm     = x_ref;
    b->v_ref_mm_s   = v_ref;
    b->a_ref_mm_s2  = a_ref;

    /* ── 5. 控制律：PD + 三个前馈（全部是解析解，见 ball.h 推导）────────
     *   theta_g = a_des/K_BALL + a_x/g        theta_b = theta_g - pitch
     * ⚠ a_x 那项分母是 g 不是 K_BALL —— (5/7) 对两项同时作用、约掉了。
     *   写成 a_x/K_BALL 会过补偿 40%（单测 test_ax_ff_uses_g_not_k 断言了它）。*/
    e  = b->x_est - x_ref;
    ev = b->v_est - v_ref;
    a_pd = -(b->kp * e + b->kd * ev);

    b->th_pd_deg    = (a_pd  / BALL_K_MM_S2_PER_RAD) * RAD2DEG;
    b->th_traj_deg  = (a_ref / BALL_K_MM_S2_PER_RAD) * RAD2DEG;
    b->th_ax_deg    = b->ff_ax_en    ? ((in->ax_mm_s2 / BALL_G_MM_S2) * RAD2DEG) : 0.0f;
    b->th_pitch_deg = b->ff_pitch_en ? (-in->pitch_deg) : 0.0f;

    /* ── 5b. 摩擦（起动阻力）前馈 ──────────────────────────────────────
     * 🔴 为什么必须有它（2026-07-31 真机把账算清了）:
     *   实测起动阈值约 0.9°(=68us)，而弱侧总权限只有 1.67°(=126us) ⇒ **摩擦吃掉一半以上**:
     *      总 204mm/s² − 起动 110mm/s² = **可用仅 94mm/s²**
     *   而轨迹前馈自己就要 78(出发)/100(返回) mm/s² ⇒ **留给 PD 的接近 0**。
     *   真机后果: `sat` 恒在 74%、一过冲就再也拉不回来、球撞到 ±120mm。
     *   ⇒ 让 PD 的输出**叠在起动阈值之上**、而不是自己从 0 爬过去，那 68us 就还回来了。
     *   这是摩擦主导系统的标准做法（Coulomb friction feedforward），不是 hack。
     *
     * ⚠ **不能用 sign(e) 直接跳变** —— 那样在目标附近会以 ±fric 抖动（stick-slip 极限环，
     *   正是我们要消掉的东西）。故在 |e| < dead 的带内**线性过渡**: 到达目标时补偿平滑归零。
     *   dead 取 fric 对应的位置误差（= 死区宽度本身），物理含义是"进了死区就不再硬推"。
     *
     * ⚠ 补偿的方向按**位置误差**取, 不按 PD 输出取。本仓库在车级位置环上踩过这个坑
     *   (SSOT: "前馈按位置误差方向叠; 按速度输出符号会末端震荡")。
     *
     * fric_deg <= 0 时整段不生效 ⇒ 默认关闭, 靠 `G<x100>` 在线开, 达标再回填 config.h。 */
    b->th_fric_deg = 0.0f;
    if (b->fric_deg > 0.0f) {
        float dead = (b->kp > 0.0f)
                   ? (b->fric_deg * (BALL_K_MM_S2_PER_RAD / RAD2DEG) / b->kp)  /* 死区宽度(mm) */
                   : 0.0f;
        float s;
        if (dead <= 0.0f)      s = (e > 0.0f) ? -1.0f : ((e < 0.0f) ? 1.0f : 0.0f);
        else if (e >  dead)    s = -1.0f;                 /* 球在 +x 侧 ⇒ 往 -x 推 */
        else if (e < -dead)    s =  1.0f;
        else                   s = -e / dead;             /* 带内线性过渡, e=0 时为 0 */
        b->th_fric_deg = b->fric_deg * s;
    }

    th_base = b->th_pd_deg + b->th_traj_deg + b->th_ax_deg +
              b->th_pitch_deg + b->th_fric_deg;

    /* Total output limit is needed by the conditional integrator as well as the final clamp. */
    lim = b->theta_max_deg;
    if (lim > BALL_THETA_MECH_MAX_DEG) lim = BALL_THETA_MECH_MAX_DEG;

    /* Weak integral: update only on a real, fresh camera interval inside the separation band.
     * Clamp the physical I angle and back-calculate its state, so no hidden windup can build behind
     * the I cap. Conditional anti-windup rejects only increments that deepen total-output
     * saturation; opposite increments remain enabled and can actively unwind the actuator. */
    b->i_active = 0;
    b->i_aw_hold = 0;
    if (b->ki <= 0.0f) {
        ball_reset_integral(b);
    } else if (!i_reset_this_step && meas_dt > 0.0f && fabsf(e) <= b->i_band_mm) {
        float old_th_i = b->th_i_deg;
        float cand_i = b->i_err_mm_s + e * meas_dt;
        float cand_th_i = -(b->ki * cand_i / BALL_K_MM_S2_PER_RAD) * RAD2DEG;
        float delta_th_i;
        float cand_total;

        b->i_active = 1;
        cand_th_i = clampf(cand_th_i, -b->i_limit_deg, b->i_limit_deg);
        cand_i = -(cand_th_i / RAD2DEG) * BALL_K_MM_S2_PER_RAD / b->ki;
        delta_th_i = cand_th_i - old_th_i;
        cand_total = th_base + cand_th_i;

        if ((cand_total > lim && delta_th_i > 0.0f) ||
            (cand_total < -lim && delta_th_i < 0.0f)) {
            b->i_aw_hold = 1;
        } else {
            b->i_err_mm_s = cand_i;
            b->th_i_deg = cand_th_i;
        }
    }

    th = th_base + b->th_i_deg;

    /* ── 6. 限幅（永远不许超过题目给的机械极限）───────────────────────── */
    lim = b->theta_max_deg;
    if (lim > BALL_THETA_MECH_MAX_DEG) lim = BALL_THETA_MECH_MAX_DEG;
    b->sat = 0;
    if (th >  lim) { th =  lim; b->sat = 1; }
    if (th < -lim) { th = -lim; b->sat = 1; }
    b->theta_cmd_deg = th;

    /* ── 7. 成绩单：峰值才是判分量（判分靠回放视频，取最坏帧）───────────── */
    b->err_mm = b->x_est - x_ref;
    ae = b->err_mm < 0.0f ? -b->err_mm : b->err_mm;
    if (ae > b->peak_abs_err_mm) b->peak_abs_err_mm = ae;

    if (theta_deg_out) *theta_deg_out = th;
    return b->state;
}

const char *ball_fail_str(ball_fail_t f)
{
    switch (f) {
    case BALL_F_NONE:    return "NONE";
    case BALL_F_NO_MEAS: return "NO_MEAS";
    case BALL_F_BADCFG:  return "BADCFG";
    case BALL_F_LIMIT:   return "LIMIT";
    default:             return "?";
    }
}
