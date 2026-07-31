#ifndef BALL_H
#define BALL_H
/*
 * ball.h - 车载滚球平衡控制层（2026 省赛 H 题：车载平衡滚球运动控制系统）
 *
 * 定位：**纯算法层，不依赖 HAL/driverlib**（只用 <math.h>）⇒ 可在 PC 上 gcc 编译并单元测试
 *       （pc_test/test_ball.c）。它不碰相机、不碰舵机、不碰引脚：
 *         car.c 每拍喂它「相机测到的球位 + 车的纵向加速度 + 车体 pitch」，
 *         它回一个「摆杆对车的目标倾角（度）」，car.c 再交给 servo.c 变成脉宽。
 *       ⇒ 与"+X 朝车头还是车尾""舵机往哪转"这类装配相关的符号**完全解耦**（那些在 config.h）。
 *
 * ── 被控对象（推导见 workbench/2026-H_.../00_深度审题与方案论证.md L2.2 与补 5）──
 *   球径 1cm、槽内半径 0.65cm ⇒ 球沉入槽底与内壁**内切于单点** ⇒ 有效滚动半径 = 球半径
 *   ⇒ 就是标准的"实心球斜面纯滚动"：
 *
 *        x'' = (5/7)·( g·sin(theta_g) - a_x·cos(theta_g) )
 *            ≈ (5/7)·( g·theta_g - a_x )              [小角度]
 *            =  K_BALL·theta_g - (5/7)·a_x
 *
 *   其中 theta_g = 摆杆**对地**倾角，a_x = 车的纵向加速度，K_BALL = (5/7)g。
 *   ⇒ **纯双积分：无阻尼、无回复力。** 题目明文禁止任何增摩擦改造（说明 7"则不予测试"）
 *     ⇒ 阻尼只能来自控制器的 D 项。校赛B 铁律 C1（先 D 后 P 最后 I）直接适用。
 *
 * ── 三个前馈都是解析解，不是补丁（这是本模块的核心，也是报告"理论分析"的采分点）──
 *   要让 x'' = a_des（PD + 轨迹要求的加速度），解上式得：
 *
 *        theta_g = a_des/K_BALL + a_x/g
 *        theta_b = theta_g - pitch          (theta_b = 摆杆**对车**倾角 = 舵机要给的量)
 *
 *   ⇒ 输出 = PD 项 + 轨迹加速度前馈 + 纵向加速度前馈 + pitch 补偿。四项各有明确出处。
 *   ⚠ 注意 a_x 那一项的分母是 **g** 而不是 K_BALL —— 因为 (5/7) 对两项同时作用、约掉了。
 *     写成 a_x/K_BALL 会让前馈**过补偿 40%**，是个很容易犯且现象隐蔽的错（本模块单测断言了它）。
 *
 * ── 为什么 pitch 补偿不是可选项（数字在这里）──
 *   摆杆的倾角基准长在一辆会晃的车上 ⇒ pitch 以 1:1 加进对地倾角。
 *   pitch 抖 0.5° ⇒ 球加速度 7.007·sin0.5° = 61 mm/s² ⇒ 纯 P 稳态偏差 = 61/kp = **6.8mm**
 *   ⇒ 单这一项就吃掉 ±10mm 预算的 68%。而它的频率（轮转频 ~1.1Hz）**在闭环带宽 0.48Hz 之外**
 *   ⇒ 控制器压根压不住，只能从源头前馈减掉。
 *
 * ── 观测器为什么带模型（不是普通 alpha-beta）──
 *   相机 30fps ⇒ 两帧之间有 33ms 空白，而控制回路跑 50Hz（CFG_BALL_MS=20ms，理由见 config.h
 *   §7.12 与报告 2.3.3）⇒ 33/20 ⇒ **约三分之一的拍没有新测量**。
 *   普通 alpha-beta 在空白期只能"假设匀速"线性外推，而球此刻正在**受控加速**。
 *   本模块把**已知的控制输入**（当前 theta_b 与 a_x）代进预测：
 *        a_known = K_BALL·(theta_b + pitch) - (5/7)·a_x
 *   相机只负责纠正残差。⇒ 位置外推误差从 0.5·a·dt²（最大摆角下约 0.4mm）降到近似为零，
 *   **速度估计的滞后改善更明显**（而 D 项吃的就是速度）。这是校赛B「从带噪测量抠位置+速度」
 *   经验的直接迁移，也是那套资产真正能搬过来的部分（那颗 VL53L0X 本题不能用，见补 1）。
 *
 * ── 三个安全默认（每条都对应一个会丢分的场景）──
 *   1. **测量超龄就摊平摆杆，不许沿用上一个角度**：相机丢帧/掉线时 (age > max_age_s)
 *      回 BALL_BLOCKED 且输出 theta=0。沿用旧角度 = 拿一个陈旧的倾角持续给球加速度，
 *      球会一路加速撞挡片（题目"若内壁有凹坑…不予测试"，球飞出去还可能卡住机构）。
 *   2. **软限位主动回中**：|x| 超过 x_soft_mm 就把 setpoint 拉向 0 并报 warn，
 *      而不是等它撞两端挡片。刻度到 ±12cm、挡片在管端 ±12.5cm ⇒ 默认软限位 ±110mm。
 *   3. **倾角限幅**：|theta| 夹在 theta_max_deg 内。机械极限来自题目 h>=5cm：
 *      右端下摆 250·sin|theta| 触板 ⇒ theta_max = 11.54° ⇒ 权限 a_max = 1.401 m/s²。
 *      默认取 6°（留一倍余量）。
 *
 * ── 判分取的是"最坏值"不是平均值 ⇒ 本模块记 peak ──
 *   题目要求 1 明文"完整记录每次测试时钢球运动的视频且能按要求回放"，说明 6 要求画面
 *   "能判定其位置"，说明 7 给了 0.1cm 刻度 ⇒ **判分介质就是回放视频 + 槽边刻度**
 *   ⇒ 整圈 30s 每一帧都在判据里，不存在"抽查瞬间稳住"的侥幸。
 *   ⇒ 所以 peak_abs_err_mm 才是要盯的量，std 会骗人。**控制目标定 ±3~5mm，不是门限 ±10mm。**
 *
 * 状态: 2026-07-29 新建。**PC 单测已过；真机零验证。** 全部增益为 [纸面] 初值
 *       （ωn=3 rad/s、ζ=0.8 ⇒ kp=9 s^-2、kd=4.8 s^-1），落地整定后回填 config.h。
 */
#include <math.h>

/* ── 物理常量（纯几何推导，不是标定值，所以写死在这里而不是 config.h）─────────── */
#define BALL_G_MM_S2        9810.0f      /* 重力加速度，mm/s^2 */
#define BALL_ROLL_COEF      (5.0f/7.0f)  /* 实心球纯滚动系数（单点接触 ⇒ 就是 5/7）*/
#define BALL_K_MM_S2_PER_RAD (BALL_ROLL_COEF * BALL_G_MM_S2)  /* = 7007.14 mm/s^2 每弧度 */

/* 摆杆机械极限：题目 h>=5cm、杆长 25cm ⇒ 右端下摆 250·sin(theta) 达 50mm 即触板。
 * 这不是我们的设计选择，是题目给的天花板 ⇒ 任何 theta_max_deg 都不该超过它。 */
#define BALL_THETA_MECH_MAX_DEG  11.54f

/* 摩擦(起动阻力)前馈的默认幅值, 度。0 = 关闭。命令 `J<度×100>` 在线改。
 * ✅ **2026-07-31 定版 0（关闭）**: 擦净管内壁与钢球后真实起动阻力 **≤0.11°**
 *   (`tools/ball_break.ps1` 实测), 而本项是按误差符号叠 ±J 的**继电器** ⇒ 给 1.20° 等于
 *   放一个比它要补的东西大 10 倍的继电器, 纯粹是极限环燃料。干净管子上的单变量扫描单调:
 *   J1.20 超限71.8%/std15.6 · J0.20 67.1%/13.9 · **J0 58.8%/11.3** ⇒ 关掉最好。
 *   随后 kp5/kd2/CENTER=1154 一起使静止 HOLD 达标(13s 259 样本, 超出 ±10mm = 0 个, 峰值 3.4mm)。
 * ⚠️ **本宏与 `config.h` 的 `CFG_BALL_FRIC_DEG` 是同一事实的两份副本, 必须同值同改。**
 * ⚠️ 若换脏管子/换球使起动阻力回升, 给值**不要超过 `ball_break.ps1` 实测的起动阻力**(超了就是继电器)。 */
#define BALL_FRIC_DEG_DEFAULT    0.0f

/* ── 在干什么 ──────────────────────────────────────────────────── */
typedef enum {
    BALL_M_OFF = 0,   /* 不控制（输出 0 度，摊平）*/
    BALL_M_HOLD,      /* 把球稳在 setpoint（要求 4/5/6，以及 IDLE 下等人摆球）*/
    BALL_M_TRAJ       /* 跑要求 3 的往返序列 O -> +amp -> -amp */
} ball_mode_t;

/* ── 干得怎么样 ────────────────────────────────────────────────── */
typedef enum {
    BALL_IDLE = 0,    /* 没在控制 */
    BALL_HOLD,        /* 正在保持 */
    BALL_TRAJ_RUN,    /* 轨迹进行中 */
    BALL_TRAJ_DONE,   /* 轨迹跑完且已 settle */
    BALL_BLOCKED      /* 干不下去，原因见 fail */
} ball_state_t;

typedef enum {
    BALL_F_NONE = 0,
    BALL_F_NO_MEAS,   /* 相机测量超龄 ⇒ 反馈不可信 ⇒ 摊平摆杆（不是沿用旧角度）*/
    BALL_F_BADCFG,    /* 增益/限幅没配（kp<=0 或 theta_max<=0）⇒ 拒绝输出，别拿默认 0 当"能跑" */
    BALL_F_LIMIT      /* 只作 warn 用：球进了软限位区，已主动回中 */
} ball_fail_t;

/* ── 每拍喂进去的测量值 ────────────────────────────────────────── */
typedef struct {
    float x_mm;         /* 相机测得的球位（mm，沿槽轴，符号与题目刻度同向）*/
    int   meas_valid;   /* 本拍有没有**新**测量。相机 30fps 而控制 50Hz ⇒ 约 1/3 的拍为 0 */
    float meas_age_s;   /* 最近一次有效测量距今多久（秒）。> max_age_s ⇒ 判反馈不可信 */
    float ax_mm_s2;     /* 车纵向加速度（mm/s^2，+ = 沿 +x 方向加速）。
                         * ⭐ 首选来源 = **速度指令的微分**（无延迟、无噪声），IMU 加计只补
                         *   "没预料到的"那部分。理由：前馈要的是"即将发生的加速度"，
                         *   而 IMU 测的是"已经发生的"，后者天生晚一拍。*/
    float pitch_deg;    /* 车体俯仰（度，+ = 使摆杆对地倾角增大的方向；符号由 car.c 统一）*/
    float dt_s;         /* 距上一拍的真实经过时间（秒）。⚠ 必须传真实 dt，不能传标称周期
                         * —— 本仓库踩过：主循环被 LCD 拖慢，"数拍×假设1ms"让 RPM 虚高 5 倍 */
} ball_in_t;

/* ── 参数 + 运行态 ─────────────────────────────────────────────── */
typedef struct {
    /* --- 控制器参数（默认 [纸面]，整定后回填 config.h）--- */
    float kp;             /* 位置增益，单位 1/s^2。ωn = sqrt(kp) */
    float kd;             /* 速度增益，单位 1/s。 ζ = kd/(2·sqrt(kp)) */
    float ki;             /* 积分增益，单位 1/s^3。0 = 关闭；运行时只改它做单变量整定 */
    float i_band_mm;      /* 积分分离带：仅 |x_est-x_ref| <= 此值且有新视觉帧时累计 */
    float i_limit_deg;    /* I 项自身角度限幅；与总输出限幅、方向 anti-windup 独立 */
    /* 摩擦(起动阻力)前馈幅值，度。<=0 = 关闭。
     * 存在理由是实测的: 起动阈值约 0.9° 而弱侧总权限仅 1.67° ⇒ 摩擦吃掉一半以上可用加速度,
     * 轨迹前馈之后留给 PD 的接近 0(真机 sat 恒 74%)。让 PD 叠在阈值之上即可把它还回来。
     * 实现与"为什么不能用 sign(e) 直接跳变"见 ball.c §5b。 */
    float fric_deg;
    float theta_max_deg;  /* 输出倾角限幅（度）。<= BALL_THETA_MECH_MAX_DEG */
    float x_soft_mm;      /* 软限位（mm）：|x| 超过就把 setpoint 拉向 0 并报 warn */
    float x_hard_mm;      /* 物理端点（mm，仅用于把软限位夹在合理范围内）*/

    /* --- 观测器参数 --- */
    float alpha;          /* 位置修正增益 [0,1] */
    float beta;           /* 速度修正增益 [0,1)。经典取 beta ≈ alpha^2/(2-alpha) 为"临界阻尼" */
    int   use_model;      /* 1 = 预测时代入已知控制输入（强烈建议 1，理由见文件头）*/
    float max_age_s;      /* 测量超龄门限（秒）*/

    /* --- 前馈开关（**做单变量对照实验用**，不是给"忘了打开"留后门）---
     * 校赛B E1 最痛的教训：抗扰那套做出来了却没在场上启用 ⇒ **两个都默认 1**。
     * 关掉它们的唯一正当用途 = 做 A/B 实验量化"前馈到底值多少毫米"（报告要这个数）。*/
    int   ff_ax_en;
    int   ff_pitch_en;

    /* --- 要求 3 的轨迹参数（默认合计 4.5s <= 5s，留 0.5s 余量）--- */
    float traj_amp_mm;    /* 航点幅值，题目是 ±50mm */
    float traj_t_out;     /* O -> +amp 用时（秒）*/
    float traj_t_dwell;   /* 在 +amp 停留（秒）—— 让"到达后折返"这个动作在录像里看得清 */
    float traj_t_back;    /* +amp -> -amp 用时（秒）*/
    float traj_t_settle;  /* 在 -amp 稳定（秒）*/

    /* --- 运行态（外部只读）--- */
    ball_mode_t  mode;
    ball_state_t state;
    ball_fail_t  fail;    /* 终止原因（仅 BALL_BLOCKED 时有意义）*/
    ball_fail_t  warn;    /* 非终止告警：现在唯一用法 = BALL_F_LIMIT（进了软限位、已回中）。
                           * ⚠ 单开一个字段的理由与 nav.t 相同：塞进 fail 会在成功那一刻被清掉，
                           *   于是"一趟贴着限位跑完"和"一趟从容跑完"报出来一模一样。*/
    float setpoint_mm;    /* 当前目标位置（HOLD 模式下由外部设；TRAJ 模式下由轨迹给）*/

    /* 观测器状态 */
    float x_est;          /* 估计位置 mm */
    float v_est;          /* 估计速度 mm/s */
    int   est_init;       /* 收到第一个测量前不许输出（否则拿 0 当真实位置）*/
    float t_since_meas;   /* 距上一个**有效测量**过了多久（秒）。
                           * ⭐ 速度修正必须除以"测量间隔"而不是"控制拍长" —— 相机 30fps 而
                           *   控制 100Hz，用拍长(10ms)会把速度修正放大 3 倍 ⇒ 观测器自激。
                           *   由本模块自己累加，不依赖调用方怎么理解 meas_age_s。*/
    unsigned long no_meas_ticks;  /* 累计有多少拍没有新测量（诊断：相机是不是在掉帧）*/

    /* 弱积分运行态。积分只在新鲜视觉帧到达时按真实帧间隔更新，避免等效 Ki 随 FPS 漂移。 */
    float i_err_mm_s;      /* ∫(x_est-x_ref)dt，单位 mm·s */

    /* 轨迹状态 */
    float traj_t;         /* 轨迹已跑多久（秒）*/
    int   traj_phase;     /* 0=出发 1=停留 2=返回 3=稳定 4=完成 */

    /* 输出与分量（**全部留出来给遥测/LCD/报告** —— "我们真的在补偿"的可视化证据，
     * 对应校赛B 把扰动估计 f_hat 画出来那招，答辩加分点）*/
    float theta_cmd_deg;  /* 本拍输出（对车倾角，已限幅）*/
    float th_pd_deg;      /* 其中 PD 贡献 */
    float th_i_deg;       /* 其中弱积分贡献（已按 i_limit_deg 限幅）*/
    float th_fric_deg;    /* 其中摩擦前馈贡献（诊断用；进遥测才能判断它是否在起作用）*/
    float th_traj_deg;    /* 其中轨迹加速度前馈贡献 */
    float th_ax_deg;      /* 其中纵向加速度前馈贡献 */
    float th_pitch_deg;   /* 其中 pitch 补偿贡献 */
    int   sat;            /* 本拍是否撞到限幅（撞限幅说明权限不够或增益过大）*/
    int   i_active;       /* 本拍满足“新鲜视觉 + 小误差带”，积分门已打开 */
    int   i_aw_hold;      /* 本拍积分增量因会加深总输出饱和而被拒绝 */

    /* 轨迹参考（遥测/画曲线用）*/
    float x_ref_mm, v_ref_mm_s, a_ref_mm_s2;

    /* --- 成绩单 --- */
    float err_mm;             /* 当前误差 = x_est - setpoint */
    float peak_abs_err_mm;    /* ⭐ 峰值绝对误差 —— **这就是判分量**（判分取最坏值）*/
    float err_wp_out_mm;      /* 到达 +amp 航点那一刻的误差（要求 3 考核点之一）*/
    float err_wp_back_mm;     /* 稳定在 -amp 时的误差（要求 3 考核点之二）*/
    float traj_total_s;       /* 轨迹总耗时（要求 3 的 <=5s 判据）*/
} ball_t;

/* 用保守默认参数初始化。PD 为 kp=9、kd=6；弱积分能力存在但 Ki 默认 0，待真机在线整定。 */
void ball_init(ball_t *b);

/* 进 HOLD 模式，把球稳在 setpoint_mm。要求 4/5/6 用它（setpoint=0 即"稳在中心点"）；
 * **IDLE 下也要用它** —— 否则人没法把 1cm 钢球放到光滑槽的 O 点（一松手就滚走）。 */
void ball_set_hold(ball_t *b, float setpoint_mm);

/* 开一次要求 3 的往返轨迹：O -> +amp -> 折返 -> -amp 稳定。amp<=0 时用 traj_amp_mm 默认值。 */
void ball_start_traj(ball_t *b, float amp_mm);

/* 停：回 BALL_IDLE，输出 0 度（摊平）。不动参数与观测器标定。 */
void ball_abort(ball_t *b);

/* 清积分状态与诊断标志，不改 Ki/误差带/限幅参数。切目标、切模式、失去反馈时必须调用。 */
void ball_reset_integral(ball_t *b);

/* 清成绩单（peak/航点误差）。每次正式测试开始前调一次，否则峰值会跨趟累积。 */
void ball_reset_stats(ball_t *b);

/*
 * 走一拍。返回当前状态，并把**摆杆对车的目标倾角（度）**写进 *theta_deg_out。
 *   BALL_IDLE / BALL_BLOCKED -> theta=0（摊平）
 *   其余                     -> theta = PD + 弱I + 轨迹/a_x/pitch/摩擦前馈，已限幅
 * 幂等：TRAJ_DONE 之后再调会自动转入 HOLD（继续把球稳在 -amp），不会自己重跑轨迹。
 */
ball_state_t ball_step(ball_t *b, const ball_in_t *in, float *theta_deg_out);

/*
 * 轨迹采样（纯 t 的函数，与状态无关）⇒ **可单独单测**，也可离线画出整条轨迹。
 * t 从 0 起算；写回参考位置/速度/加速度（mm, mm/s, mm/s^2）。
 * 返回 0 = 轨迹已结束（t 超出总时长），此时给出末端保持值。
 * 剖面 = 每段**三角形加速度**（前半段 +a、后半段 -a）⇒ 段末速度精确为 0，
 * 且 a_ref 是解析可得的常数 ⇒ 前馈项干净。段内位移 S、时长 T ⇒ a = 4S/T^2。
 */
int ball_traj_sample(const ball_t *b, float t, float *x, float *v, float *a);

/* 轨迹总时长（秒）= t_out + t_dwell + t_back + t_settle。用于开跑前先检查 <=5s。 */
float ball_traj_duration(const ball_t *b);

/* 失败原因 -> 短字符串（遥测/串口打印用，不分配内存）。 */
const char *ball_fail_str(ball_fail_t f);

#endif /* BALL_H */

/* ── 接入还差什么（相机与机构到手后）─────────────────────────────────
 * 1. car.c 加一个模式（建议 m12）：每拍
 *      ball_in_t in = { .x_mm = <相机帧解析出的球位>, .meas_valid = <本拍有新帧?>,
 *                       .meas_age_s = <帧龄>, .ax_mm_s2 = <速度指令微分>,
 *                       .pitch_deg = <attitude_t.pitch * CFG_BALL_PITCH_SIGN>,
 *                       .dt_s = <真实 dt> };
 *      ball_step(&g_ball, &in, &th);
 *      servo_set_deg(th * CFG_BALL_SERVO_SIGN + CFG_BALL_SERVO_MID_DEG);
 *    ⚠ 两个符号常量必须由真机实验定，**不能猜**（做法见审题文档补 4.5 的 S1/S2 两个实验）：
 *      S1 车静止给固定小角度看球往哪滚 ⇒ 定 theta -> x 的符号
 *      S2 摆杆锁水平推车看球往哪滚     ⇒ 定 a_x  -> x 的符号
 *    搞反的现象是**球被前馈推向发散**，酷似"PID 怎么都调不出来"（本仓库在编码器符号、
 *    CFG_YAW_SIGN 上都踩过同类坑）。
 * 2. **相机没到手也能验**：固件已有 `$V,id,cx,cy,area*HH` 整行帧解析 + `V` 诊断命令 +
 *    tools/vision_test.ps1（PC 假装相机发帧）⇒ 可以先用 PC 灌假球位把整条链在板上跑通。
 * 3. 别忘本仓库那条坑：**新模块必须同时进 makefile**（uart_frame.h 当过"只有 .h 的孤儿"，
 *    于是"视觉串口链已就绪"是假象）。ball.obj 要加进 gcc/makefile 的 OBJECTS 与依赖规则。
 * ─────────────────────────────────────────────────────────────── */
