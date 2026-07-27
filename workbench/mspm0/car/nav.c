/*
 * nav.c - 车级导航层实现（走 N mm 带航向保持 / 原地转 N 度）。纯算法, 不依赖 HAL。
 *
 * PC 验证:
 *   cd pc_test && gcc -O2 -Wall -I.. -o test_nav test_nav.c ../nav.c ../attitude.c -lm && ./test_nav
 *
 * 设计取舍与"为什么"写在 nav.h; 本文件只写实现细节里**不看代码会踩的坑**。
 * 状态: 2026-07-27 新建。PC 单测已过, **真机零验证**（参数全 [估计]）。
 */
#include "nav.h"
#include "config.h"
#include <math.h>

#define NAV_STALL_RPM      5.0f    /* 走直: |平均转速| 低于此视为"没动" */
#define NAV_STALL_DPS      3.0f    /* 转角: |角速度| 低于此视为"没转" */
#define NAV_WZ_LP          0.3f    /* 编码器兜底时角速度差分的低通系数(越小越平滑) */

static float fclampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float fsignf(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

void nav_init(nav_t *n)
{
    /* 参数默认值全部来自 config.h —— 赛场只翻那一个文件(工作台规范 §2 铁律) */
    n->counts_per_mm  = ENC_COUNTS_PER_MM;      /* 2026-07-27 已落地标定 = 5.109 */
    n->counts_per_deg = ENC_COUNTS_PER_DEG;
    n->v_cruise       = CFG_NAV_V_CRUISE;
    n->v_min          = CFG_NAV_V_MIN;
    n->accel_rpm_s    = CFG_NAV_ACCEL;
    n->decel_mm       = CFG_NAV_DECEL_MM;
    n->tol_mm         = CFG_NAV_TOL_MM;
    n->coast_mm       = CFG_NAV_COAST_MM;
    n->kp_hdg         = CFG_KP_HDG;
    n->kd_hdg         = CFG_KD_HDG;
    n->w_max          = CFG_NAV_W_MAX;
    n->hdg_max_deg    = CFG_NAV_HDG_MAX_DEG;
    n->kp_turn        = CFG_KP_TURN;
    n->kd_turn        = CFG_KD_TURN;
    n->turn_w_max     = CFG_TURN_W_MAX;
    n->turn_w_min     = CFG_TURN_W_MIN;
    n->turn_tol_deg   = CFG_TURN_TOL_DEG;
    n->turn_settle_s  = (float)CFG_TURN_SETTLE_MS / 1000.0f;
    n->stall_s        = (float)CFG_NAV_STALL_MS  / 1000.0f;
    nav_abort(n);
}

int nav_calibrated(const nav_t *n) { return (n->counts_per_mm > 0.0f) ? 1 : 0; }

float nav_counts_to_mm(const nav_t *n, long counts)
{
    if (n->counts_per_mm <= 0.0f) return 0.0f;      /* 未标定: 不猜, 交由调用方判 */
    return (float)counts / n->counts_per_mm;
}

const char *nav_fail_str(nav_fail_t f)
{
    switch (f) {
        case NAV_F_NO_CAL:     return "NO_CAL";     /* 先跑 run_straight.ps1 标 ENC_COUNTS_PER_MM */
        case NAV_F_STALL:      return "STALL";      /* 撞墙/卡死/打滑/电机没通电 */
        case NAV_F_NO_HDG:     return "NO_HDG";     /* 发 k 标定, 或标 ENC_COUNTS_PER_DEG 走兜底 */
        case NAV_F_OFFCOURSE:  return "OFFCOURSE";  /* 航向偏太多, 已主动停 */
        default:               return "-";
    }
}

void nav_abort(nav_t *n)
{
    n->mode = NAV_M_NONE;
    n->state = NAV_IDLE;
    n->fail = NAV_F_NONE;
    n->warn = NAV_F_NONE;
    n->v_ramp = 0.0f;
    n->settle_t = 0.0f;
    n->stall_t = 0.0f;
    n->wz_est = 0.0f;
    n->ang_prev = 0.0f;
}

static void nav_begin(nav_t *n, long cl, long cr, float yaw_now)
{
    n->counts0_l = cl;
    n->counts0_r = cr;
    n->yaw0      = yaw_now;
    n->state     = NAV_RUN;
    n->fail      = NAV_F_NONE;
    n->warn      = NAV_F_NONE;
    n->v_ramp    = 0.0f;
    n->settle_t  = 0.0f;
    n->stall_t   = 0.0f;
    n->wz_est    = 0.0f;
    n->ang_prev  = 0.0f;
    n->hdg_used  = 0;
    n->done_mm   = 0.0f;
    n->done_deg  = 0.0f;
    n->err_mm    = 0.0f;
    n->err_deg   = 0.0f;
    n->peak_hdg_deg = 0.0f;
}

void nav_start_straight(nav_t *n, float mm, long cl, long cr, float yaw_now)
{
    nav_begin(n, cl, cr, yaw_now);
    n->mode    = NAV_M_STRAIGHT;
    n->tgt_mm  = mm;
    n->tgt_deg = 0.0f;
    n->err_mm  = mm;
}

void nav_start_turn(nav_t *n, float deg, long cl, long cr, float yaw_now)
{
    nav_begin(n, cl, cr, yaw_now);
    n->mode    = NAV_M_TURN;
    n->tgt_deg = deg;
    n->tgt_mm  = 0.0f;
    n->err_deg = deg;
}

/*
 * 当前"相对起点转过的角度"(度, 左正)。两条路, 优先陀螺:
 *   ① heading_ok=1 -> 陀螺积分的 yaw 差值。**不做 wrap180** —— yaw 是连续累计量,
 *      "转 720°"这类目标必须能表达(attitude.h 就是为此让 yaw 连续的)。
 *   ② 否则 counts_per_deg>0 -> (右轮增量 - 左轮增量)/counts_per_deg。右轮多走 = 左转 = 正,
 *      与 car_drive_mix(左=v-w, 右=v+w) 的 w>0=左转 符号一致。
 * *src: 1=陀螺 2=编码器兜底 0=都没有
 */
static float nav_angle(const nav_t *n, const nav_in_t *in, int *src)
{
    if (in->heading_ok) { *src = 1; return in->yaw_deg - n->yaw0; }
    if (n->counts_per_deg > 0.0f) {
        long dl = in->counts_l - n->counts0_l;
        long dr = in->counts_r - n->counts0_r;
        *src = 2;
        return (float)(dr - dl) / n->counts_per_deg;
    }
    *src = 0;
    return 0.0f;
}

/* 角速度: 陀螺直接给; 兜底模式靠角度差分 + 一阶低通(差分噪声大, 不滤会让 D 项乱抖)。 */
static float nav_rate(nav_t *n, const nav_in_t *in, int src, float ang)
{
    if (src == 1) return in->wz_dps;
    if (in->dt_s > 1e-6f) {
        float raw = (ang - n->ang_prev) / in->dt_s;
        n->wz_est += NAV_WZ_LP * (raw - n->wz_est);
    }
    n->ang_prev = ang;
    return n->wz_est;
}

static nav_state_t nav_finish(nav_t *n, nav_fail_t f, int *v, int *w)
{
    *v = 0; *w = 0;
    n->state = (f == NAV_F_NONE) ? NAV_DONE : NAV_BLOCKED;
    n->fail  = f;
    return n->state;
}

nav_state_t nav_step(nav_t *n, const nav_in_t *in, int *v_rpm, int *w_rpm)
{
    *v_rpm = 0;
    *w_rpm = 0;

    /* 幂等: 到位/失败后再调只回 0, 不自己重新起跑(否则超时自停后会"复活") */
    if (n->state != NAV_RUN) return n->state;

    if (n->mode == NAV_M_STRAIGHT) {
        /* ---- 里程 ---- */
        if (!nav_calibrated(n))
            return nav_finish(n, NAV_F_NO_CAL, v_rpm, w_rpm);   /* 未标定绝不"当 1.0 用" */

        long  davg = ((in->counts_l - n->counts0_l) + (in->counts_r - n->counts0_r)) / 2;
        float dist = nav_counts_to_mm(n, davg);
        float rem  = n->tgt_mm - dist;
        n->done_mm = dist;
        n->err_mm  = rem;

        /* ---- 到位判据: 预留刹车滑行余量 ----
         * PWM 归零后车不是立刻停, 而是靠摩擦滑一段。实测(2026-07-27 两趟 n300, 末端 v_min=35RPM
         * ≈107mm/s): 滑行 16.4mm 与 14.3mm, **只差 2mm ⇒ 确定性、可直接减掉**。
         * 同两趟的误差预算: 滑行贡献 14~16mm、标定只贡献 1~4mm、"提前进容差就停"抵掉 4~9mm
         *   ⇒ 净 +10~15mm 超程由滑行支配, 所以补偿它比调 tol_mm 或磨标定常数都值。
         * 实现: 沿**行进方向**的剩余距离降到 max(coast_mm, tol_mm) 就收油。
         *   用 rem_dir(带方向) 而不是 fabsf(rem), 于是"已经冲过头"(rem_dir 变负) 也会立刻结束
         *   —— 旧的对称判据在大幅冲过头时会掉头往回追, 而往回追之后还会再滑一次, 越追越乱。
         * coast_mm=0 时退化为旧行为(阈值就是 tol_mm), PC 单测据此保持向后兼容。 */
        float rem_dir = fsignf(n->tgt_mm) * rem;                  /* 沿行进方向还剩多少 */
        float stop_at = n->coast_mm > n->tol_mm ? n->coast_mm : n->tol_mm;
        if (rem_dir <= stop_at)
            return nav_finish(n, NAV_F_NONE, v_rpm, w_rpm);      /* 到位: 立刻停, 不 settle
                                                                  * (走直没有"回摆"问题, 车是被摩擦刹住的) */

        /* ---- 速度: 梯形斜坡 + 提前减速 ----
         * 起步走斜坡 = 防打滑(打滑同时污染里程与航向, 一次毁两个测量);
         * 末端按剩余距离线性降速, 但不低于 v_min —— 低于死区电机根本不动, 会停在离目标几 cm 处
         * 干嗡嗡, 那种"卡住"看起来像控制器坏了, 其实只是指令小于死区。 */
        n->v_ramp += n->accel_rpm_s * in->dt_s;
        if (n->v_ramp > n->v_cruise) n->v_ramp = n->v_cruise;
        float v_allow = n->v_cruise;
        if (n->decel_mm > 0.0f) v_allow = n->v_cruise * fabsf(rem) / n->decel_mm;
        float vmag = n->v_ramp < v_allow ? n->v_ramp : v_allow;
        if (vmag < n->v_min) vmag = n->v_min;
        float v = fsignf(rem) * vmag;

        /* ---- 航向保持 ----
         * 目标 = 保持起跑那一刻的航向 ⇒ 误差 = -(已转过的角度)。
         * 这里用 wrap180: "保持某个航向"本质是 mod-360 的概念, 且能防住 yaw 长期累计后
         * 误差项变成天文数字。真跑偏的病态情况由下面的 OFFCOURSE 闸门抓。 */
        int   src = 0;
        float ang = nav_angle(n, in, &src);
        float rate = nav_rate(n, in, src, ang);
        n->hdg_used = src;
        n->done_deg = ang;
        float herr = attitude_wrap180(-ang);
        if (fabsf(herr) > n->peak_hdg_deg) n->peak_hdg_deg = fabsf(herr);
        n->err_deg = herr;

        if (src != 0) {
            if (n->hdg_max_deg > 0.0f && fabsf(herr) > n->hdg_max_deg)
                return nav_finish(n, NAV_F_OFFCOURSE, v_rpm, w_rpm);
            /* w>0 = 左转。herr>0 表示车已偏右(角度变负) ⇒ 需要左转补回来 ⇒ 同号, 直接用。
             * D 项对角速度取负 = 阻尼: 正在往左转就少给点左, 防蛇行。 */
            float w = n->kp_hdg * herr - n->kd_hdg * rate;
            *w_rpm = (int)fclampf(w, -n->w_max, n->w_max);
        } else {
            /* 陀螺没标定 + 没有编码器兜底: 不纠偏(退化成原 m7)。**不是失败**, 但必须如实说 —— 写
             * warn 而不是 fail, 因为 fail 会在任务成功时被清掉(见 nav.h warn 字段的注释)。 */
            n->warn = NAV_F_NO_HDG;
            *w_rpm = 0;
        }

        /* ---- 卡住检测 ---- */
        if (fabsf(in->rpm_avg) < NAV_STALL_RPM) n->stall_t += in->dt_s;
        else                                    n->stall_t = 0.0f;
        if (n->stall_s > 0.0f && n->stall_t >= n->stall_s)
            return nav_finish(n, NAV_F_STALL, v_rpm, w_rpm);

        *v_rpm = (int)v;
        return NAV_RUN;
    }

    if (n->mode == NAV_M_TURN) {
        int   src = 0;
        float ang = nav_angle(n, in, &src);
        if (src == 0)
            return nav_finish(n, NAV_F_NO_HDG, v_rpm, w_rpm);   /* 转角没角度传感器 = 做不了闭环 */
        float rate = nav_rate(n, in, src, ang);
        n->hdg_used = src;
        n->done_deg = ang;

        /* 转角误差不 wrap: 支持 |目标| > 180°(转一圈半这类) */
        float err = n->tgt_deg - ang;
        n->err_deg = err;

        /* 到位判据 = **在容差内保持 turn_settle_s**, 不是"碰到一次就算到"。
         * 理由: 转角有惯量, 冲过去的瞬间误差也会穿过 0 —— 只看瞬时会把"正在飞过去"报成成功。
         * settle 期间仍然继续闭环(w 照算), 所以它同时也是"回摆能被压住"的证据。 */
        if (fabsf(err) <= n->turn_tol_deg && fabsf(rate) <= NAV_STALL_DPS * 3.0f) {
            n->settle_t += in->dt_s;
            if (n->settle_t >= n->turn_settle_s)
                return nav_finish(n, NAV_F_NONE, v_rpm, w_rpm);
        } else {
            n->settle_t = 0.0f;
        }

        float w = n->kp_turn * err - n->kd_turn * rate;
        /* 死区前馈: |误差| 还在容差外但指令太小 ⇒ 抬到 turn_w_min, **符号取误差方向**。
         * ⚠ 不能按 PID 输出符号叠 —— 位置环精定位就因此在末端狂震过一次(config.h §5)。 */
        if (fabsf(err) > n->turn_tol_deg && fabsf(w) < n->turn_w_min)
            w = fsignf(err) * n->turn_w_min;
        *w_rpm = (int)fclampf(w, -n->turn_w_max, n->turn_w_max);
        *v_rpm = 0;                      /* 原地转: 线速度恒 0 */

        /* 卡住检测(转角版): 给了指令但车不转 */
        if (fabsf(rate) < NAV_STALL_DPS) n->stall_t += in->dt_s;
        else                             n->stall_t = 0.0f;
        if (n->stall_s > 0.0f && n->stall_t >= n->stall_s)
            return nav_finish(n, NAV_F_STALL, v_rpm, w_rpm);

        return NAV_RUN;
    }

    n->state = NAV_IDLE;
    return NAV_IDLE;
}
