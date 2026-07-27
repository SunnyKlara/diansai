#ifndef NAV_H
#define NAV_H
/*
 * nav.h - 车级导航层：走 N 毫米(带航向保持) / 原地转 N 度
 *         = 阶梯 2.5(里程) + 阶梯 3(航向走直) + 阶梯 4(原地转角) 的共同地基
 *
 * 定位：**纯算法层，不依赖 HAL/driverlib**（只用 <math.h> 与 attitude_wrap180）
 *       ⇒ 可在 PC 上 gcc 编译并单元测试（pc_test/test_nav.c），落地前就能把逻辑错验掉。
 *       它不碰引脚、不碰 PWM、不读编码器：**car.c 喂它测量值，它回一对车级指令 (v, w)**，
 *       car.c 再把 (v, w) 交给已真机达标的 m7 闭环差速层（左=v-w，右=v+w，单位 RPM）。
 *
 * ── 为什么必须单独一层，而不是往 car.c 里再塞一个 case ──
 *   ① 已有的 m7 只是"左右各一个速度环"：它能让两轮同速，**但它不看 yaw**，所以地面上一有
 *      左右摩擦差/轮径差/地板不平，车就会画弧线而两个速度环都以为自己干得很好。
 *      走直线的判据是"航向不变"，不是"两轮转速相等" —— 这两件事只在理想地面上等价。
 *   ② 位置环(m3)是**单轮 counts** 的，它做不了"车走 50cm"：没有 counts→mm，也没有航向。
 *   ③ 这两件事（里程、航向）算错的代价是"车跑歪/撞墙"，属于落地前必须先在 PC 上验掉的逻辑。
 *
 * ── 三个安全默认（每一条都对应一个已知会翻车的场景）──
 *   1. **未标定就拒绝走 mm**：counts_per_mm <= 0 时 nav_step() 直接回 NAV_BLOCKED /
 *      NAV_F_NO_CAL，而不是"当 1.0 用"。config.h 里 ENC_COUNTS_PER_MM 现在就是 0.0f，
 *      若默默当 1 用，"走 500mm" 会变成"走 500counts"≈几厘米，看起来像"电机没劲"。
 *   2. **航向不可信就不纠偏**：heading_ok=0（没做过 `k` 零偏标定/天顶向量无效）时 w 恒为 0,
 *      退化成原来的 m7 行为，并在状态里留下 NAV_F_NO_HDG 让上层能打印出来。
 *      拿没标定的陀螺去纠偏 = 主动把车拐歪，比不纠偏更坏。
 *   3. **卡住要有名字**：给了速度但里程不涨(撞墙/轮子卡死/打滑)超过 stall_s 就 NAV_BLOCKED /
 *      NAV_F_STALL，不闷头顶着电机烧。落地测试里"车没动"与"命令没到"长得一模一样，
 *      必须由固件这一侧给出区分（无线丢下行命令已在 2026-07-27 落地首跑实际发生过）。
 *
 * ── 两条从本仓库真机教训里搬来的设计 ──
 *   · **缓起步（梯形斜坡）**：起步猛给速度会打滑，而打滑会**同时**污染里程(编码器多转)与
 *     航向(车头被甩偏) —— 一次打滑毁掉两个测量。所以 v 走斜坡，不是阶跃。
 *   · **死区前馈按误差方向叠**：转角末端 |w| 太小时电机根本不动(实测死区≈10% PWM、且左轮比
 *     右轮更高)，所以 |err| > 容差时给一个 turn_w_min 的最小指令，**符号取误差方向**。
 *     ⚠ 不能按"PID 输出符号"叠 —— 位置环精定位就因此在末端狂震过一次（见 config.h §5）。
 *
 * 状态: 2026-07-27 新建。**PC 单测已过；真机零验证**（全部参数标 [估计]，落地整定后回填 config.h）。
 */
#include "attitude.h"   /* 只用 attitude_wrap180()：角度归一化只该有一份实现 */

/* ── 当前在干什么 ──────────────────────────────────────────────── */
typedef enum {
    NAV_M_NONE = 0,     /* 没有导航任务 */
    NAV_M_STRAIGHT,     /* 走直 N mm（带航向保持） */
    NAV_M_TURN          /* 原地转 N 度 */
} nav_mode_t;

/* ── 干得怎么样（失败也要有名字）─────────────────────────────────── */
typedef enum {
    NAV_IDLE = 0,       /* 空闲：没任务 */
    NAV_RUN,            /* 正在走/转 */
    NAV_DONE,           /* 到位（已在容差内 settle 够久） */
    NAV_BLOCKED         /* 干不下去了，原因看 nav_t.fail */
} nav_state_t;

typedef enum {
    NAV_F_NONE = 0,
    NAV_F_NO_CAL,       /* counts_per_mm 未标定 ⇒ 任何 mm 目标都无意义 */
    NAV_F_STALL,        /* 给了速度但里程不涨：撞墙/卡死/打滑/电机没通电 */
    NAV_F_NO_HDG,       /* 拿不到角度：陀螺没标定 且 编码器兜底也没标定 ⇒ 转角无法闭环 */
    NAV_F_OFFCOURSE     /* 走直时航向已偏出 hdg_max_deg ⇒ 停，别再横着往前冲 */
} nav_fail_t;

/* ── 每拍喂进去的测量值 ────────────────────────────────────────── */
typedef struct {
    long  counts_l;     /* 左轮累计计数（前进为正）= encoder_count(ENC_1) */
    long  counts_r;     /* 右轮累计计数（前进为正）= encoder_count(ENC_2) */
    float yaw_deg;      /* 当前偏航角（度，连续累计，左转为正）= attitude_t.yaw */
    float wz_dps;       /* 当前偏航角速度（度/秒）—— 转角环的 D 项、也用于判 settle */
    float rpm_avg;      /* 左右转速平均（RPM）—— 只用于走直的 stall 判据 */
    float dt_s;         /* 距上一拍的真实经过时间（秒）。⚠ 必须传真实 dt，不能传标称周期 */
    int   heading_ok;   /* 1=陀螺已零偏标定且天顶向量有效（car.c 传 g_up_valid） */
} nav_in_t;

/*
 * ★ 为什么要左右分开传，而不是只传平均值：
 *   左右计数**之差**就是一个不依赖 IMU 的转角计（右轮多走 ⇒ 车左转）。这是 guide 里那条
 *   "有不依赖 IMU 的编码器兜底"的落地实现 —— 陀螺没标定/坏了/轴没定，走直与转角**仍然能做**，
 *   只是精度换成"轮子不打滑"这个假设。原地转打滑严重时它会骗人（坑库有条），所以它是
 *   **兜底不是首选**：heading_ok=1 时永远优先用陀螺。
 */

/* ── 参数 + 运行态 ─────────────────────────────────────────────── */
typedef struct {
    /* --- 参数（默认值来自 config.h，运行时可改，整定完回填）--- */
    float counts_per_mm;    /* 里程标定：编码器计数/毫米。<=0 表示未标定 */
    float counts_per_deg;   /* 转角标定：(右-左) 计数/度，编码器兜底用。<=0 = 没兜底 */
    float v_cruise;         /* 走直巡航速度 (RPM) */
    float v_min;            /* 走得动的最低速度 (RPM)：末端不许低于它，否则原地嗡嗡不前进 */
    float accel_rpm_s;      /* 速度斜坡 (RPM/秒)：缓起步防打滑 */
    float decel_mm;         /* 提前减速距离 (mm)：剩这么远开始线性降速 */
    float tol_mm;           /* 走直到位容差 (mm) */
    float kp_hdg, kd_hdg;   /* 航向保持 PD：输出单位 = 差速 RPM（kp: RPM/度, kd: RPM/(度/秒)）*/
    float w_max;            /* 航向修正上限 (RPM)：别让纠偏把车拐成原地转 */
    float hdg_max_deg;      /* 走直允许的最大航向偏差 (度)：超了判 OFFCOURSE 停车 */
    float kp_turn, kd_turn; /* 原地转角 PD（同上单位）*/
    float turn_w_max;       /* 转角角速度指令上限 (RPM) */
    float turn_w_min;       /* 转角死区前馈 (RPM)：|err|>容差时的最小指令，按误差方向叠 */
    float turn_tol_deg;     /* 转角到位容差 (度) */
    float turn_settle_s;    /* 到位后还要在容差内保持这么久才算 DONE（防冲过去又回摆就宣布成功）*/
    float stall_s;          /* 给了速度但不动多久算卡住 (秒) */

    /* --- 运行态（外部只读）--- */
    nav_mode_t  mode;
    nav_state_t state;
    nav_fail_t  fail;       /* **终止**原因（只在 NAV_BLOCKED 时有意义）*/
    nav_fail_t  warn;       /* **非终止**告警：任务照做完，但有一句必须如实说的话。
                             * 现在唯一的用法 = NAV_F_NO_HDG（走直时没有任何航向来源 ⇒
                             * 这趟纯靠两轮同速，"走直了"不能当成航向环的功劳）。
                             * ⚠ 为什么要单开一个字段：PC 单测抓到过 —— 早先把它塞进 fail，
                             * 结果任务成功时 fail 被清成 NONE，**告警正好在打成绩单那一刻消失**，
                             * 于是一趟"没纠偏的走直"会被报成和"航向环压住了"一模一样。 */
    float tgt_mm;           /* 本次走直目标 (mm, 带符号) */
    float tgt_deg;          /* 本次转角目标 (度, 相对起点, 左正) */
    long  counts0_l;        /* 起点计数（左） */
    long  counts0_r;        /* 起点计数（右） */
    float yaw0;             /* 起点偏航（走直=要保持的航向；转角=转角基准）*/
    float v_ramp;           /* 斜坡后的当前速度指令 (RPM) */
    float settle_t;         /* 已在容差内保持了多久 (秒) */
    float stall_t;          /* 已经"给速度但不动"多久 (秒) */
    float wz_est;           /* 编码器兜底模式下由角度差分出来的角速度（陀螺可用时不用它） */
    float ang_prev;         /* 上一拍的角度（算 wz_est 用） */
    int   hdg_used;         /* 1=用陀螺纠偏 2=用编码器兜底 0=没纠偏（成绩单要如实说）*/

    /* --- 结果（跑完给人看的"成绩单"）--- */
    float done_mm;          /* 实际走了多少 mm */
    float done_deg;         /* 实际转了多少度 */
    float err_mm;           /* 剩余距离误差 mm（DONE 时即最终误差） */
    float err_deg;          /* 剩余角度误差 度 */
    float peak_hdg_deg;     /* 走直过程中的最大航向偏差（走得直不直的量化判据） */
} nav_t;

/* 用 config.h 的默认参数初始化（含 ENC_COUNTS_PER_MM，可能是 0=未标定）。 */
void nav_init(nav_t *n);

/* 开一次走直：mm 带符号(负=倒车)；counts_l/r、yaw_now = 当前测量值，用作起点基准。
 * 注意"要保持的航向"就是**此刻的航向** ⇒ 调用前把车摆正，或先发 `o` 把 yaw 归零。 */
void nav_start_straight(nav_t *n, float mm, long counts_l, long counts_r, float yaw_now);

/* 开一次原地转：deg 带符号(左转为正)，相对**此刻**的航向。 */
void nav_start_turn(nav_t *n, float deg, long counts_l, long counts_r, float yaw_now);

/* 停：清任务，回 NAV_IDLE（不动参数）。car.c 的 `z`/超时自停会调它。 */
void nav_abort(nav_t *n);

/*
 * 走一拍。返回当前状态，并把车级指令写进 *v_rpm / *w_rpm（单位 RPM，直接喂 m7 的 v/r）。
 *   NAV_RUN     -> 按 v/w 驱动
 *   NAV_DONE    -> v=w=0（到位，上层可停机并打成绩单）
 *   NAV_BLOCKED -> v=w=0（看 n->fail 给原因）
 *   NAV_IDLE    -> v=w=0
 * 幂等：DONE/BLOCKED 之后再调只会继续回 0，不会自己重新起跑。
 */
nav_state_t nav_step(nav_t *n, const nav_in_t *in, int *v_rpm, int *w_rpm);

/* 计数 <-> 毫米（未标定时返回 0，调用方需自己判 nav_calibrated()）。 */
float nav_counts_to_mm(const nav_t *n, long counts);
int   nav_calibrated(const nav_t *n);

/* 失败原因 -> 短字符串（遥测/串口打印用，不分配内存）。 */
const char *nav_fail_str(nav_fail_t f);

#endif /* NAV_H */
