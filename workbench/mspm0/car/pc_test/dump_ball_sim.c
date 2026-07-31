/*
 * dump_ball_sim.c - 把滚球控制层的仿真过程导成 CSV，供设计报告出图
 *
 * 为什么单独写这个而不在 Python 里重算一遍模型：
 *   报告里的曲线必须是**我们真正烧进固件的那份 ball.c 跑出来的**。若在 Python 里
 *   重新实现一遍动力学与控制律，图好看但证明不了固件的行为 —— 那正是"看着专业、
 *   实际是编的"。本程序直接链接 ../ball.c，与 test_ball.c 用同一个被控对象仿真。
 *
 * ⚠ 边界（报告 4.3.2 已写明）：这里的被控对象就是 ball.h 推导的那个方程本身，两者同源。
 *   故这些曲线证明的是"控制律在该模型下的行为"，不证明模型描述了真实的钢球。
 *   模型本身要靠真机实验验证（固定 2 度倾角、量球走 50mm 的时间，理论 0.64s）。
 *
 * 编译运行(在本目录)：
 *   gcc -O2 -Wall -I.. -o dump_ball_sim.exe dump_ball_sim.c ../ball.c -lm
 *   ./dump_ball_sim.exe          -> 在 ../../../2026-H_.../03_报告/figs/data/ 下写 3 个 csv
 */
#include "ball.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define DEG2RADf 0.017453292519943295f
/* ⚠ 输出目录必须**全 ASCII**：本仓库铁律"GCC/C 运行库不认中文路径"（同 car/ 工程为什么
 * 不放在 天猛星主板平台/ 下）。实测把 CSV 直接写进报告目录（路径含中文）时 fopen 全部失败。
 * ⇒ 数据落在这里，由出图脚本(python，能吃中文路径)读走并把 PDF 写进报告的 figs/。 */
#define OUTDIR "_sim/"

/* ---- 被控对象仿真：与 test_ball.c 逐字相同 ---- */
typedef struct { float x, v; } plant_t;

static void plant_step(plant_t *p, float theta_b_deg, float pitch_deg, float ax, float dt)
{
    float th_g = (theta_b_deg + pitch_deg) * DEG2RADf;
    float a = BALL_K_MM_S2_PER_RAD * sinf(th_g) - BALL_ROLL_COEF * ax;
    p->x += p->v * dt + 0.5f * a * dt * dt;
    p->v += a * dt;
}

/* 按 config.h §7.12 的定版参数配置（这里手写一份，避免 ball.c 依赖 config.h） */
static void cfg(ball_t *b)
{
    ball_init(b);
    b->kp = 9.0f;  b->kd = 6.0f;        /* zeta = 1.0，依据见 ball.c 注释 */
    b->theta_max_deg = 6.0f;
    b->alpha = 0.5f; b->beta = 0.1667f; b->use_model = 1;
    b->max_age_s = 0.15f;
    b->ff_ax_en = 1; b->ff_pitch_en = 1;
}

/* 控制回路频率必须等于固件的 1000/CFG_BALL_MS = 50Hz。
 * 报告里那 8 张图的图注写着"曲线由固件 ball.c 导出"，若这里的节拍与固件不同，
 * 图形状虽然差不多、但那句话就是假的（原先这里写 200Hz）。
 * 单步 20ms 直接积分是**物理正确**的：舵机指令在一拍内保持不变（零阶保持），
 * 而 plant_step 对恒加速度是闭式解，故不需要再细分子步。 */
#define SIM_CTRL_HZ  50.0f
#define SIM_CAM_FPS  30.0f

/*
 * 跑一段仿真并写 CSV。
 *   traj    = 1 起动要求3 往返轨迹 / 0 保持在 setpoint
 */
/* kd_override > 0 时用它替换 cfg() 的 kd —— 用来做阻尼比 A/B（zeta = kd/(2*sqrt(kp))）。
 * 报告 2.3.1 节"为什么把 zeta 从 0.8 提到 1.0"那一段的数字必须由它产出，
 * 否则那是个无法复现的断言。 */
static void run_kd(const char *path, float seconds, int traj, float setpoint,
                   float x0, float ax, float pitch, int ff_ax, int ff_pitch,
                   float kd_override)
{
    ball_t b; plant_t p; ball_in_t in;
    FILE *f = fopen(path, "w");
    float dt = 1.0f / SIM_CTRL_HZ, cam_dt = 1.0f / SIM_CAM_FPS, t_cam = cam_dt, age = 0.0f, th = 0.0f;
    int n = (int)(seconds * SIM_CTRL_HZ + 0.5f), i;

    if (!f) { printf("  [X] cannot write %s\n", path); return; }
    cfg(&b);
    if (kd_override > 0.0f) b.kd = kd_override;
    b.ff_ax_en = ff_ax; b.ff_pitch_en = ff_pitch;
    p.x = x0; p.v = 0.0f;
    if (traj) ball_start_traj(&b, 0.0f); else ball_set_hold(&b, setpoint);

    /* 列名与物理量单位写在表头里 —— 出图脚本按名字取列，不靠列序 */
    fprintf(f, "t_s,x_true_mm,x_est_mm,x_ref_mm,v_est_mms,theta_deg,"
               "th_pd,th_traj,th_ax,th_pitch,err_mm,peak_mm,sat\n");

    for (i = 0; i < n; i++) {
        int have = 0;
        t_cam += dt;
        if (t_cam >= cam_dt) { t_cam = 0.0f; have = 1; }
        age = have ? 0.0f : (age + dt);

        in.x_mm = p.x;  in.meas_valid = have;  in.meas_age_s = age;
        in.ax_mm_s2 = ax;  in.pitch_deg = pitch;  in.dt_s = dt;

        ball_step(&b, &in, &th);
        fprintf(f, "%.4f,%.4f,%.4f,%.4f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.4f,%.4f,%d\n",
                i * dt, p.x, b.x_est, b.x_ref_mm, b.v_est, th,
                b.th_pd_deg, b.th_traj_deg, b.th_ax_deg, b.th_pitch_deg,
                b.err_mm, b.peak_abs_err_mm, b.sat);
        plant_step(&p, th, pitch, ax, dt);
    }
    fclose(f);
    printf("  wrote %-22s  n=%d  peak=%.3fmm  wpOUT=%.3f  wpBACK=%.3f  t=%.2fs  kd=%.2f\n",
           strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
           n, b.peak_abs_err_mm, b.err_wp_out_mm, b.err_wp_back_mm, b.traj_total_s, b.kd);
}

static void run(const char *path, float seconds, int traj, float setpoint,
                float x0, float ax, float pitch, int ff_ax, int ff_pitch)
{
    run_kd(path, seconds, traj, setpoint, x0, ax, pitch, ff_ax, ff_pitch, -1.0f);
}

int main(void)
{
    printf("dump_ball_sim: K=%.2f mm/s^2 per rad, theta_mech_max=%.2f deg\n",
           BALL_K_MM_S2_PER_RAD, BALL_THETA_MECH_MAX_DEG);

    /* 图1 要求3 往返轨迹：跟踪 + 摆角四分量分解 */
    run(OUTDIR "sim_traj.csv", 5.2f, 1, 0.0f, 0.0f, 0.0f, 0.0f, 1, 1);

    /* 图2 加速度前馈 A/B 对照：车持续以 0.3 m/s^2 加速（= 我们的限幅值） */
    run(OUTDIR "sim_ff_ax_off.csv", 4.0f, 0, 0.0f, 0.0f, 300.0f, 0.0f, 0, 1);
    run(OUTDIR "sim_ff_ax_on.csv",  4.0f, 0, 0.0f, 0.0f, 300.0f, 0.0f, 1, 1);

    /* 图3 俯仰补偿 A/B 对照：车体持续 0.5 度俯仰（地面不平的典型量级） */
    run(OUTDIR "sim_ff_pit_off.csv", 4.0f, 0, 0.0f, 0.0f, 0.0f, 0.5f, 1, 0);
    run(OUTDIR "sim_ff_pit_on.csv",  4.0f, 0, 0.0f, 0.0f, 0.0f, 0.5f, 1, 1);

    /* 图4 阶跃响应（zeta=1.0 应无过冲）：球初始偏 30mm，目标回中心 */
    run(OUTDIR "sim_step30.csv", 3.0f, 0, 0.0f, 30.0f, 0.0f, 0.0f, 1, 1);

    /* 阻尼比 A/B：kd=4.8 -> zeta=0.8（常用值）  vs  kd=6.0 -> zeta=1.0（本设计）。
     * 同一 kp=9、同一 30mm 阶跃、同一往返轨迹，只改 kd 一个量。 */
    run_kd(OUTDIR "sim_step30_z08.csv", 3.0f, 0, 0.0f, 30.0f, 0.0f, 0.0f, 1, 1, 4.8f);
    run_kd(OUTDIR "sim_traj_z08.csv",   5.2f, 1, 0.0f,  0.0f, 0.0f, 0.0f, 1, 1, 4.8f);

    printf("done.\n");
    return 0;
}
