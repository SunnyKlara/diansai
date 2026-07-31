/*
 * car.c - 天猛星小车 电机控制全栈框架 (电流环 / 速度环 / 位置环 + 串口在线调参)
 *
 * 模式(串口 m<n> 切换): 0=IDLE  1=CURRENT  2=SPEED  3=POSITION  4=OPEN(直给PWM)
 * 目标(t<v>) 含义随模式: 电流mA / 转速RPM / 位置counts(相对启动) / PWM%   (带符号=方向)
 * 在线调参(改"当前模式对应环"的增益): p<v>/i<v>/d<v>, 单位千分之(如 p200 => Kp=0.200)
 *   z=急停回IDLE   ?=打印状态
 * 遥测(~100ms): [ctl] MODE tgt | I:i1,i2 | V:v1,v2(RPM) | PWM:p1,p2 | C:c1,c2(计数)
 *
 * 真机状态(诚实标注):
 *   - 电流环: 电流反馈今日已真机验证, 可直接整定。
 *   - 速度环/位置环 + 编码器: // 待真机验证 —— 编码器 A/B 须先经 1k/2k 分压(5V→3.3V)
 *     再接 PA7/PB19/PB20/PB21, 否则灌坏脚。
 *   - PWM 上限 PWM_CAP=60%: 保护 7.4V 电机(跑在 12V 母线)。
 *   - ENC_CPR(每圈计数)为占位值, 必须"手转一圈实测"标定后改写, 否则 RPM 只是相对量。
 */
#include "ti_msp_dl_config.h"
#include "gc9a01.h"
#include "motor.h"
#include "uart_dbg.h"
#include "control.h"
#include "encoder.h"
#include "imu.h"        /* ICM42688 六轴驱动 (真机验活通过 a808122: WHOAMI=0x47/|a|=0.995g) */
#include "attitude.h"   /* 六轴姿态解算 (纯算法层, 已 PC 单测验证) */
#include "nav.h"        /* 车级导航: 走 N mm(带航向保持) / 原地转 N 度 (纯算法层, 已 PC 单测) */
#include "magnet.h"     /* 电磁铁(板载第三颗 DRV8231, PB0 单向) —— 阶梯 7 的"手" // 待真机验证 */
#include "uart_frame.h" /* 视觉坐标帧解析 ($V,id,cx,cy,area*HH) —— 纯算法层, 已 PC 单测 */
#include "vservo.h"     /* 反应式视觉伺服控制律 (static inline 纯逻辑, 已 PC 单测) */
#include "cmd_gate.h"   /* 串口命令格式门 (纯逻辑, 挡 ESP boot 日志误触发命令; 已 PC 单测) */
#include "servo.h"      /* 转向舵机(TIMG12_C1=PA31, 50Hz) —— 阿克曼前轮转向 // 待真机验证 */
#include "linesens.h"   /* 循迹模块串口链路(UART1=PB4/PB5) —— 现阶段只嗅探不解析 // 真机零验证 */
#include "lineframe.h"  /* 八路巡线 $D/$A 帧解析 (纯算法层, 已 PC 单测) */
#include "line.h"       /* 循迹算法层: 归一化/标定/PD -> 横向偏差+转向 (纯算法层, 已 PC 单测) */
#include "task.h"       /* 第2项任务层: 按键/计时/到达判定/安全停 (纯算法层, PC 单测 47/47) */
#include "beep.h"       /* 蜂鸣器非阻塞状态机 (纯算法层, PC 单测 24/24) */
#include "disp_run.h"   /* LCD RUN 页纯排版: 出6行文本+变化位掩码 (纯算法层, PC 单测 31/31) */
#include "ball.h"       /* H题第3/4/5/6项: 车载滚球平衡 (纯算法层, PC 单测 14 组全过) // 真机零验证 */
#include <math.h>       /* 水平仪页用 sqrtf/fabsf/atan2f (软浮点, 仅 IDLE 下的显示页调用) */

/* ★所有可调参数(时基/周期/PWM上限/ENC_CPR/PID增益/死区/容差/调试开关)已集中到 config.h。
 * 本文件只写逻辑, 不再散落 #define —— 赛场调参只翻 config.h(工作台规范 §2 铁律)。
 * 参数的出处/实测依据写在 config.h 每个宏旁边; 事实真值源见 .kiro/steering/工程事实SSOT.md。 */
#include "config.h"

static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

#if CFG_LINE_UART_EN
/* 前向声明: 这三个循迹嗅探辅助函数定义在 poll_uart 之前(它们要用 g_st),
 * 而命令分发在更早的位置就要调它们。 */
static void line_wait_ms(uint32_t ms);
static void line_selftest(void);
static void line_sweep_query(void);
#endif

/* ==== 模式 ==== */
enum { MODE_IDLE = 0, MODE_CURRENT, MODE_SPEED, MODE_POSITION, MODE_OPEN, MODE_DUAL,
       MODE_DRIVE, MODE_DRIVE_CL, MODE_NAV_S, MODE_NAV_T, MODE_VSERVO,
       MODE_LINE,                 /* m11: H题第2项 —— 按键启动 + 循迹跑一圈 + 检到启停线停车 */
       MODE_BALL,                 /* m12: H题第3项 —— 车静止, 摆杆把球稳在目标位/跑往返轨迹 */
       MODE_N };
static const char *mode_name[MODE_N] = { "IDLE", "CURR", "SPD", "POS", "OPEN", "DUAL", "DRV", "DRVC",
                                        "NAVS", "NAVT", "VSRV", "LINE", "BALL" };

/* ==== 运行时(串口可改) ==== */
static volatile int g_mode   = MODE_IDLE;
static volatile int g_target = 0;       /* mA / RPM / counts / PWM% (随模式) */
static volatile int g_print_ms = PRINT_MS;   /* 遥测周期(真实ms). 整定时 f20 调快抓暂态, f100 复原 */
static uint32_t g_tele_seq = 0;              /* 遥测行号(行尾 #<seq>): 无线丢包只能靠它算, 见打印处注释 */
/* 位置环精定位(2026-07-25, 运行时可调免重烧): 死区前馈% + 到位死区counts */
static int g_pwm_dz  = CFG_POS_FF_DZ;   /* 死区前馈%(默认值+依据见 config.h). 运行时命令 w<v> 可改 */
/* 速度环死区补偿（差速/转角/导航共用的 drive_closed_loop 用）。默认见 config.h §5；
 * 运行时命令 `W<%>` 设置运动时最小 PWM；W0 同时关闭方向化 breakaway，完整退回旧行为。 */
static int g_drv_dz  = CFG_DRV_FF_DZ;
static int g_pos_tol = CFG_POS_TOL;     /* 到位死区counts(默认值+依据见 config.h). 运行时命令 e<v> 可改 */
static int g_m1duty  = 0, g_m2duty = 0;  /* MODE_DUAL 调试(m5): 独立每电机 PWM%(命令 x/y), 每拍读双电流, 专门测通道串扰 */
/* 差速层(m6 DRIVE 开环 / m7 DRIVE_CL 闭环)的车级指令: 命令 v<线速度> / r<角速度>。
 * m6 单位=PWM%(开环, 用来验差速层与左右分组是否正确, 离地即可验);
 * m7 单位=RPM(左右目标各喂一个已达标的速度环 —— 两轮机械不匹配, 同 PWM 走不直, 必须双轮独立闭环)。 */
static int g_dv = 0, g_dw = 0;
/* ==== 车级导航（m8 走 N mm / m9 转 N 度）====
 * g_nav 是导航层的全部状态与参数(nav.h)。**它自己产出 (v,w) 然后借道 g_dv/g_dw 走 m7 那条
 * 已真机达标的闭环差速路径** —— 不另开一条驱动通路, 所以 `z`/超时自停/PWM 限幅全部照旧生效,
 * 遥测的 D: 字段也直接就是"导航此刻在要多少速度", 不用新增字段就能看出它在干什么。 */
static nav_t g_nav;

/* ==== m11 循迹任务（H题第2项）==== */
#if CFG_LINE_UART_EN
/* `g_lf` 定义在 linesens.c 里且**刻意非 static**（那边注释写着"car.c 要读它出横向偏差"）。
 * extern 声明暂时放这儿而不是 linesens.h —— 那个文件正被另一条线改，避免撞车；
 * ⬜ 赛后把这一行搬进 linesens.h。 */
extern lf_t g_lf;
static line_t   g_line;             /* 循迹算法层状态(含现场白/黑标定) */
static task_t   g_task;             /* 任务状态机(按键/计时/到达/安全停) */
static beep_t   g_beep;             /* 蜂鸣器非阻塞状态机 */
static uint32_t g_lost_seg  = 0;    /* 本趟丢线**段数**(不是毫秒): 屏上 L<n>, 一眼看出这趟干净不干净 */
static int      g_was_lost  = 0;    /* 上一拍是否在丢线(用来数"段") */
static float    g_line_err  = 0.0f; /* 最近一次横向偏差 mm(遥测/排障) */
/* 启停线判据的运行态（判据本身与三层防误触的理由写在 config.h §7.8 的"启停线判据"块）。
 * 为什么这三个量必须进遥测: MIN_ON 是几何算出来的 4, 但**实际能不能达到 4 取决于探头离地高度
 * 与胶带宽度** ⇒ 跑完看 `on=cur/max` 就知道该不该改门限, 不用靠猜、也不用重烧去试。 */
static int      g_cross_on  = 0;    /* 去毛刺后的"正压在启停线上"标志(喂给 task 层) */
static int      g_cross_run = 0;    /* 已连续成立几拍 */
static int      g_cross_cur = 0;    /* 本拍在线通道数 */
static int      g_cross_max = 0;    /* 本趟见过的最大在线通道数 */
/* 本趟"判到启停线"的**上升沿次数**。为什么要单独记而不看瞬时 g_cross_on:
 * ① 台架验收时只能靠 `?` 抽样, 抽中那 2~5 拍的概率极低, 但计数会一直留着;
 * ② 真机跑完能区分两种完全不同的失败 —— xcnt=0 是"判据没触发(探头/门限问题)",
 *    xcnt>=1 而没停是"触发了但被 task 层 ARM_BLIND_MM/LAP_MIN_MM 挡掉(门限问题)"。
 *    没这个数就只能重烧去猜是哪一种。 */
static uint32_t g_cross_cnt = 0;
static float    g_w_lp      = 0.0f; /* |w| 低通 —— 分段速度的曲率代理(为什么不用 |err| 见 config.h) */
static int      g_vseg_now  = 0;    /* 本拍分段速度给出的巡航 RPM(遥测/排障) */
/* 分段速度三个旋钮的**运行时副本**(开机取 config.h 默认, 命令 A/H/D 在线改)。
 * 理由同 g_line.kp/kd: 整定要"跑一趟→看数→改一个值→再跑一趟"迭代好几轮,
 * 每轮重烧 140s 不但慢, 还直撞 SSOT §D2 禁忌 2(连续快烧把本芯片怼进 lockup 过一次)。
 * ⚠ 只活在 RAM, 断电即失 ⇒ **整定出来的值必须回填 config.h 并 commit**("达标即锁死")。 */
static float    g_vs_lo     = CFG_LINE_VSEG_W_LO;
static float    g_vs_hi     = CFG_LINE_VSEG_W_HI;
static int      g_vs_slow   = CFG_LINE_VSEG_V_SLOW;
/* 启停线门限的运行时副本(命令 N)。为什么它也必须能在线改: 门限 4 是**几何算出来的**,
 * 但实际能盖到几路取决于探头离地高度与胶带宽度 —— 而这两样只能在真板上量。
 * 若只能改 config.h, 台架上一发现"on_max 只到 3"就得重烧 142s 再试, 一轮试错半小时。 */
static int      g_cross_min = CFG_LINE_CROSS_MIN_ON;
/* 模块装车朝向: 1 = X1 在车左(pos[0] 取正) / 0 = X1 在车右。命令 `R<0|1>` 在线翻。
 * 为什么必须能在线翻: 搞反了车会**朝反方向跑飞**(越偏越往外打舵), 而判据只要"压 x1 看 err 符号"
 * 30 秒就能验完 —— 若只能改 config.h, 一次验错要赔 142s 重烧, 且极可能是在车已经跑飞之后。 */
static int      g_x1_left   = CFG_LINE_X1_ON_LEFT;
static int      g_line_st   = 0;    /* 最近一次 line_state_t */
static uint32_t g_task_run0 = 0;    /* 起跑那一拍的 SysTick(打成绩单用) */
static int      g_task_v    = 0;    /* 任务层要的速度档(0STOP/1CRUISE/2SLOW) */
static int      g_line_v_cruise = 0; /* m11 巡航速度在线覆盖 RPM; 0 = 用 CFG_TASK_V_CRUISE。
                                      * 只活在 RAM ⇒ 整定出达标值必须回填 config.h 再 commit。 */
static int      g_task_disp = 0;    /* 任务层说"该刷屏了" */
static int      g_beep_lv   = 0;    /* 蜂鸣器当前电平(没接线时也算, 便于遥测看见) */
static uint32_t g_btn_virt_until = 0;  /* 虚拟按键按下到期时刻 ms(命令 K); 0 = 没在按 */
static float    g_ball_mm   = DISP_RUN_NO_BALL;  /* 球位 mm; 第4/5/6项接上相机后填, 现在恒无效 */
#endif
/* ==== 视觉链（m10 VSRV）====
 * g_uf = 帧解析器(喂字节, 出目标+新鲜度)  g_vs = 伺服控制律参数(来自 config.h §7.7)
 * ★ 帧从**现有两个串口**进来(见 poll_uart 里的 '$' 分流) ⇒ 不用第三路 UART、不用相机就能测:
 *   PC 直接发 `$V,1,200,240,900*HH` 就能让车按"看到目标"去动。相机到手后接哪个口都行。 */
static uf_parser_t g_uf;

/* 🔴 注入通道**专用**解析器 —— 与相机的 g_uf 严格分开。
 *
 * 为什么必须分开(2026-07-31 真机 bug, 我自己接相机时引入的):
 *   `vision_grab()` 的判据是 `ch == '$' || g_uf.in_frame`。当相机与调试口**共用** g_uf 时,
 *   相机每 40ms 灌一帧 17 字节, 那 1.5ms 里 `in_frame` 恒为 1 —— 此刻调试口来的**任何**字符都会被
 *   当成相机帧的一部分吞掉、不进命令通道。
 *   概率算得出来且与实测吻合: 1.5/40 = 3.75%/字符, 命令按 25ms/字符发 ⇒ 6 字符命令至少丢一个 ≈ 20%。
 *     · 丢首字母 ⇒ 整条被格式门拒掉 ⇒ 实测现象 `U1226: board reports us=1086`(脉宽没变)
 *     · **丢中间数字 ⇒ `U1226` 变 `U126`, 会被接受并给出一个错的脉宽 —— 静默且危险**
 *   代价已实证: 它让一次 Sweep 整体报废(丢命令的那点从未通电 ⇒ 中位估计被拉到 ≈0us ⇒ 赶球一直朝
 *   一边推 ⇒ 球顶端点 ⇒ 后续全部 guard hit)。**一条丢失的命令报废整次运行。**
 *   以前没暴露, 只因为这条路径上从来没有真相机在灌帧。
 *
 * 分开之后: 相机字节 → g_uf(唯一入口是 SysTick 抽出来的环形缓冲); 调试/ESP 的 `$` 行 → g_uf_inj。
 * 两者的 in_frame 互不可见 ⇒ 相机再怎么灌也吞不掉命令字符。
 * 解析成功的注入帧**并入 g_uf.last**, 于是下游(uf_get / BALL: 遥测 / LCD / m10 / m12)完全不必知道
 * 帧是从哪来的 —— "PC 假装相机"这个能力(bring-up 检查 3/4)一个字都不用改。 */
static uf_parser_t g_uf_inj;
static vs_cfg_t    g_vs;
static uint32_t    g_vs_lost_ms = 0;   /* 连续拿不到新鲜目标多久了(ms) */
/* ==== H题滚球平衡(m12) ====
 * g_ball 的参数由 ball_apply_cfg() 从 config.h §7.12 灌进去 —— ball.c 刻意不 include config.h
 * (保持纯算法层 / PC 单测零依赖), 所以"配置的落点"在这里而不是 ball.c 里。
 * g_ball_stamp: uf_get() 在同一帧内反复调用都会返 1(数据没变), 而观测器必须区分"这一拍有没有
 *   **新**测量" —— 靠帧的 stamp_ms 变没变来判。这是"相机 30fps + 控制回路更快"的必然要求。 */
static ball_t      g_ball;
static uint32_t    g_ball_stamp = 0;
static int         g_ball_stamp_init = 0;
static float       g_ball_v_prev = 0.0f; /* 上一拍车级线速度 mm/s, 用于把速度指令微分成 a_x */
/* ==== IMU / 航向状态（详见文件后半 "姿态/航向" 段的职责分工说明） ==== */
static attitude_t g_att;                 /* 姿态状态(yaw/pitch/roll + 陀螺零偏) */
static int   g_imu_ok   = 0;             /* imu_init 是否读到 0x47 */
static int   g_cal_left = 0;             /* >0 = 零偏标定进行中, 还差几个样本(命令 k 启动) */
static float g_wz_dps   = 0.0f;          /* 去零偏(+死区)后的偏航角速度(dps), 遥测/将来当航向环 D 项 */
/* 定轴两参数做成运行时可改(命令 a<0|1|2> / s<±1>): 定轴要试多种组合, 若靠重烧就会触发
 * "连续快烧"这个已把芯片怼进 lockup 的禁忌(SSOT §D2)。默认值来自 config.h, 定完回填锁死。 */
static int   g_yaw_axis = CFG_YAW_AXIS;
static int   g_yaw_sign = CFG_YAW_SIGN;
/* ★ 偏航主路径 = "把角速度投影到测得的天顶方向"(见 attitude.h attitude_yaw_rate 的理由)。
 * g_up   : 静止标定(命令 k)时由加速度均值归一化得到的**天顶单位向量**(传感器系)
 * g_gb_raw: 同次标定得到的**原始系**陀螺零偏(投影前扣除)
 * g_up_valid=0 时自动回落到"挑最近轴 + 循环置换"的老路径(CFG_YAW_AXIS)。 */
static float g_up[3]     = { 0.0f, 0.0f, 1.0f };
static float g_gb_raw[3] = { 0.0f, 0.0f, 0.0f };
static int   g_up_valid  = 0;
static float g_cal_a[3]  = { 0.0f, 0.0f, 0.0f };   /* 标定累加器: 加速度(原始系) */
static float g_cal_g[3]  = { 0.0f, 0.0f, 0.0f };   /* 标定累加器: 陀螺(原始系) */
/* ==== LCD 页面状态(水平仪页详见后文 "水平仪页" 段) ==== */
static int   g_disp = CFG_DISP_BOOT_PAGE;  /* 0=编码器计数页 1=水平仪页; 命令 u<0|1>, 默认见 config.h */
static int   g_disp_dirty = 0;           /* 切页后需要重画计数页静态层 */
static int   g_lv_static = 0;            /* 水平仪页静态层(标题+容差环)是否已画 */
static int16_t g_lv_px = -999, g_lv_py = -999;   /* 小球上次屏坐标(相对圆心) */
static char  g_lv_txt[2][20] = { { 0 }, { 0 } }; /* 两行文字上次内容 -> 仅变化才重绘 */
/* ==== 运动安全: 超时自停(两个闸门, 语义与取舍见 config.h §7) ====
 * g_cmd_at  : 最近一次收到命令的时刻 -> 静默超时(没人说话 N 秒就停)
 * g_mode_at : 进入当前模式的时刻     -> 硬上限(任何命令都不能续命)
 * 单位都是 g_st(SysTick 5kHz 计数), 与其它节拍同一时基。 */
static volatile uint32_t g_st      = 0;   /* SysTick 计数(5kHz), 全局时基; 在 SysTick_Handler 里 ++ */
static volatile uint32_t g_cmd_at  = 0;
static volatile uint32_t g_mode_at = 0;
static int g_run_ms_ovr = 0;   /* >0 = 运行时覆盖静默超时(命令 h<ms>); 0 = 用 config.h 的按模式默认 */

/* 增益: 索引 0=电流环 1=速度环 2=位置环。默认值 + 整定依据/实测数据全在 config.h。
 * 运行时可用 p/i/d<×1000> 改"当前模式对应环"的增益; 整定达标后回填 config.h 锁死。 */
static float gkp[3] = { CFG_KP_CUR, CFG_KP_SPD, CFG_KP_POS };
static float gki[3] = { CFG_KI_CUR, CFG_KI_SPD, CFG_KI_POS };
static float gkd[3] = { CFG_KD_CUR, CFG_KD_SPD, CFG_KD_POS };

static pid_t pid_i[2], pid_v[2], pid_p[2];   /* 每电机一份 */
static int   i_meas[2] = { 0, 0 };            /* 最近电流(遥测用) */

static void apply_gains(void)
{
    for (int m = 0; m < 2; m++) {
        pid_set_gains(&pid_i[m], gkp[0], gki[0], gkd[0]);
        pid_set_gains(&pid_v[m], gkp[1], gki[1], gkd[1]);
        pid_set_gains(&pid_p[m], gkp[2], gki[2], gkd[2]);
    }
}
static void reset_all_pid(void)
{
    for (int m = 0; m < 2; m++) { pid_reset(&pid_i[m]); pid_reset(&pid_v[m]); pid_reset(&pid_p[m]); }
}
/* 全停 —— 命令 `z` 与超时自停**共用这一个实现**, 两条路不许分叉。
 * (2026-07-27 真机踩过的坑: 当时 z 只清 g_target、留着 g_dv/g_dw, 遥测还显示 D:100,0,
 *  再进一次 m6/m7 车就按旧速度窜出去。抽成一个函数后, 新增指令变量只需改这一处。) */
static void stop_all(void)
{
    g_mode = MODE_IDLE; g_target = 0;
    g_dv = 0; g_dw = 0; g_m1duty = 0; g_m2duty = 0;
    nav_abort(&g_nav);      /* 导航任务也必须忘掉 —— 否则 `z` 之后再进 m8 会接着走剩下的距离 */
    ball_abort(&g_ball);    /* z/timeout must also discard ball-mode integral and trajectory history. */
#if CFG_LINE_UART_EN
    /* m11 任务也必须一起收尾(同一条急停路径, 不复制第二份逻辑 —— 见上面那条 2026-07-27 的坑)。
     * ⚠ 只在 RUN/BRAKE 时才 abort: task_abort 对 DONE 态的语义是"复位回 IDLE",
     *   那会把刚跑完那趟的**冻结走时抹掉** —— 而那正是我们要念给评委/记进日志的数。 */
    if (g_task.state == TASK_RUN || g_task.state == TASK_BRAKE)
        task_abort(&g_task, g_st / ST_PER_MS, TASK_FAIL_MANUAL);
#endif
    reset_all_pid();
    /* ⚠ **有意不动电磁铁**(不在这里 magnet_off())。理由: `z` 的语义是"停止运动", 而松开电磁铁
     * 是一个**有后果的动作** —— 车正夹着钢球时急停就把球掉在半路, 比停在原地更糟(还得重新找球)。
     * 过热风险已由 CFG_MAG_MAX_ON_MS 那道独立闸门兜住, 不需要在这里重复。要放就显式发 `E0`。 */
}
/* 当前模式的静默超时(ms)。0 = 不看门(IDLE)。默认值与理由见 config.h §7; h<ms> 可运行时覆盖。 */
static uint32_t run_limit_ms(int mode)
{
    if (g_run_ms_ovr > 0) return (uint32_t)g_run_ms_ovr;
    switch (mode) {
        case MODE_CURRENT:  return CFG_RUN_MS_CURR;
        case MODE_SPEED:    return CFG_RUN_MS_SPD;
        case MODE_POSITION: return CFG_RUN_MS_POS;
        case MODE_OPEN:     return CFG_RUN_MS_OPEN;
        case MODE_DUAL:     return CFG_RUN_MS_DUAL;
        case MODE_DRIVE:    return CFG_RUN_MS_DRIVE;
        case MODE_DRIVE_CL: return CFG_RUN_MS_DRIVE_CL;
        case MODE_NAV_S:
        case MODE_NAV_T:    return CFG_RUN_MS_NAV;
        /* 视觉伺服: 目标帧本身就在持续到达, 但**帧不是命令**(不刷新静默时钟) ⇒ 这道闸门仍然有效,
         * 它挡的是"相机在发、车在追、但没人管了"。追一个目标给 8s。 */
        case MODE_VSERVO:   return CFG_RUN_MS_VSERVO;
        /* m11: 按键一按就没人再发命令了(说明 4 禁止人为干涉) ⇒ 这道闸门要宽到装得下一整圈。
         * ⚠ 真正的墙是 CFG_RUN_MS_HARDCAP, 见 config.h §7.11 那条警告。 */
        case MODE_LINE:     return CFG_RUN_MS_LINE;
        /* m12: 摆杆整定时人在看球、不一定持续发命令; 给 20s。车在本模式不动 ⇒ 超时风险只是
         * "摆杆继续动着没人管", 比循迹跑飞轻得多。硬上限仍是全局 15s(见下), 刻意不放大。 */
        case MODE_BALL:     return CFG_RUN_MS_BALL;
        default:            return 0;
    }
}
/* 硬上限 —— **按模式取值**。为什么要分模式（2026-07-29 真机逼出来的）：
 *   循迹一圈 6141.6mm，在整定出的 188mm/s 下要 **约 33s**，而全局硬上限是 15s
 *   ⇒ 不分模式就永远跑不完一圈、整圈验收无从做起。
 *   但硬上限是最后一道安全墙（`m4 OPEN` 全开环、`m6` 开环差速这些一旦跑飞 45s 会撞坏东西）
 *   ⇒ **只给 MODE_LINE 放大**，其余模式一律保持 15s 不动。
 * ⚠ 它仍然"不可绕过"：`h<ms>` 只改静默超时，改不了这里。 */
static uint32_t hardcap_ms(void)
{
#if CFG_LINE_UART_EN
    if (g_mode == MODE_LINE) return (uint32_t)CFG_RUN_MS_HARDCAP_LINE;
#endif
    return (uint32_t)CFG_RUN_MS_HARDCAP;
}

static int loop_index(void)   /* 当前模式对应的增益索引; 无环返回 -1 */
{
    if (g_mode == MODE_CURRENT)  return 0;
    if (g_mode == MODE_SPEED)    return 1;
    if (g_mode == MODE_DRIVE_CL) return 1;   /* 闭环差速走的就是速度环 -> p/i/d 直接调它 */
    if (g_mode == MODE_POSITION) return 2;
    return -1;                               /* m8/m9 的增益不在 gkp[] 里, 见 nav_gain_cmd() */
}
/* m8/m9 下的 p/i/d 路由 —— 航向/转角增益**住在 g_nav 里**(nav.h 是它们的单一真值源),
 * 不进 gkp[] 数组: 一个参数两处副本, 改一处漏一处是本仓库反复踩过的坑。
 * 返回 1 = 本命令已被导航层吃掉, run_cmd 不用再走 gkp 那条路。
 * ⚠ 没有 I: 航向保持与转角都是**纯 PD** —— 电机死区下积分会 hunt(位置环同因, config.h §5)。 */
static int nav_gain_cmd(char c, int v)
{
    float f = v / 1000.0f;
    if (g_mode == MODE_NAV_S) {
        if      (c == 'p') g_nav.kp_hdg = f;
        else if (c == 'd') g_nav.kd_hdg = f;
        else if (c == 'i') uart_dbg_puts("[nav] 航向保持是纯 PD, 无 I(死区下积分会 hunt)\n");
        return 1;
    }
    if (g_mode == MODE_NAV_T) {
        if      (c == 'p') g_nav.kp_turn = f;
        else if (c == 'd') g_nav.kd_turn = f;
        else if (c == 'i') uart_dbg_puts("[nav] 转角是纯 PD, 无 I\n");
        return 1;
    }
#if CFG_LINE_UART_EN
    /* m11 循迹的增益**住在 g_line 里**(同 nav 的理由: 一个参数两处副本必漏)。
     * 不接这一条的后果很实际: 每调一次 Kp/Kd 就要重烧一次 140s 的固件, 与本仓库
     * "单变量 + 定量裁决 + 快速迭代"的整定方法论直接冲突(2026-07-29 落地前发现并补上)。
     * ⚠ `i` 被**复用**成转向限幅 w_max(RPM, 直接取 v 不除 1000) —— 循迹是纯 PD 没有 I 项,
     *   而 w_max 是整定时真正需要动的第三个旋钮(它决定"多大偏差就打满舵")。 */
    if (g_mode == MODE_LINE) {
        if      (c == 'p') { g_line.kp = f;
                             uart_dbg_puts("[line] kp="); uart_dbg_put_int((int)(g_line.kp * 1000.0f)); }
        else if (c == 'd') { g_line.kd = f;
                             uart_dbg_puts("[line] kd="); uart_dbg_put_int((int)(g_line.kd * 1000.0f)); }
        else if (c == 'i') { g_line.w_max = (float)v;      /* 注意: 不除 1000 */
                             uart_dbg_puts("[line] w_max(RPM)="); uart_dbg_put_int((int)g_line.w_max); }
        uart_dbg_puts(" (x1000; i=w_max 直接给 RPM)\n");
        return 1;
    }
#endif
    return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

#if CFG_LINE_UART_EN
/* 按装车朝向铺 pos[]（左为正，中线为 0；8 路 ±42/±30/±18/±6 @12mm 间距）。
 * ⚠ **只写 pos[]，绝不重调 `line_init`** —— line_init 会把 `have_w/have_b` 清零并把增益
 *   重置成 config 默认值 ⇒ 在线翻朝向会顺手抹掉"开机自动置好的标定"(cal 变 NO ⇒ `line_step`
 *   恒返回 NOCAL 拒绝转向) 和这一轮整定出来的 kp/kd/w_max。这个副作用不会报错, 只会表现为
 *   "翻了个方向循迹就不动了", 极难归因。 */
static void line_pos_apply(void)
{
    int i;
    float s = g_x1_left ? +1.0f : -1.0f;
    for (i = 0; i < LF_CH; i++)
        g_line.pos[i] = s * (((float)(LF_CH - 1) / 2.0f) - (float)i) * (float)CFG_LINE_PITCH_MM;
}

/* 分段速度三个值的回显。W_LO/W_HI 打 ×10 的整数(uart_dbg 没有浮点格式化, 全仓统一这么打)。 */
static void print_vseg(void)
{
    uart_dbg_puts("[vseg] en=");   uart_dbg_put_int(CFG_LINE_VSEG_EN);
    uart_dbg_puts(" lo(x10)=");    uart_dbg_put_int((int)(g_vs_lo * 10.0f));
    uart_dbg_puts(" hi(x10)=");    uart_dbg_put_int((int)(g_vs_hi * 10.0f));
    uart_dbg_puts(" slow(RPM)=");  uart_dbg_put_int(g_vs_slow);
    uart_dbg_puts(" xmin=");       uart_dbg_put_int(g_cross_min);   /* 启停线门限(命令 N) */
    /* 朝向连 pos[0] 一起打: 光看 x1left= 还要脑内换算符号, 直接把生效的 pos[0] 摊出来最省事。
     * 判据 —— 压住 x1 那一路时 `err` 的符号必须与 pos0 同号。 */
    uart_dbg_puts(" x1left=");     uart_dbg_put_int(g_x1_left);
    uart_dbg_puts(" pos0=");       uart_dbg_put_int((int)g_line.pos[0]);
    uart_dbg_puts("\n");
}

/* 循迹的在线旋钮(**大写 A/H/D/N/R**, 小写全被占了 —— a=定竖直轴 h=静默超时 d=Kd n=走直 r=角速度)。
 *   A<x10>  |w_lp| 的"直线阈" W_LO ×10   (A120 => 12.0)
 *   H<x10>  |w_lp| 的"弯道阈" W_HI ×10   (H400 => 40.0)   H = HI
 *   D<rpm>  弯道速度 V_SLOW              (D55)             D = Down-speed
 *   N<n>    启停线门限 = 至少几路在线     (N3 / N4)
 *   R<0|1>  模块装车朝向(X1 在左=1)
 * 前四条 **参数 0 = 回 config.h 默认**(同 t0/c0 的既有约定, 少记一套规则); R 是例外, 见其分支。
 * 把 D 设成等于当前巡航速度就等价于关掉分段(下游有 v_curve=min(V_SLOW,v_fast) 的 clamp)
 * ⇒ 不必再给 EN 开关一条在线命令。
 * ⚠ **选字母前必须先查占用, 不能凭"小写被占了就用大写"**: 本函数最初取了 `B`/`L`, 而
 *   `B`=`linesens_set_baud`、`L`=`linesens_dump`(打 8 路位型, 循迹最常用的调试命令) 早就占着,
 *   而本拦截又放在 switch **之前** ⇒ 那两条命令会被静默吃掉、连报错都没有。
 *   查法: `Select-String -Path car.c -Pattern "case '(.)'" -AllMatches -CaseSensitive`
 *   ——⚠ 必须带 `-CaseSensitive`, 否则 PowerShell 的 Sort -Unique 会把 `E/e`、`C/c` 合并、
 *   给出一张"看起来全被占了"的假表(我第一次就是这么被误导的)。
 *   2026-07-31 占用实况: 大写已用 `B C E F G K L M Q S T U V W X Y`, 空闲 `A D H I J N O P R Z`。 */
static int vseg_cmd(char c, int v)
{
    if      (c == 'A') { g_vs_lo   = (v > 0) ? (float)v / 10.0f : CFG_LINE_VSEG_W_LO; }
    else if (c == 'H') { g_vs_hi   = (v > 0) ? (float)v / 10.0f : CFG_LINE_VSEG_W_HI; }
    else if (c == 'D') { g_vs_slow = (v > 0) ? v                : CFG_LINE_VSEG_V_SLOW; }
    /* 夹到 1..8: 0 会让判据恒成立(每拍都"压在启停线上"), >8 则永不成立 —— 两头都是静默失效,
     * 现场很难看出来, 所以在入口就夹死而不是信任手输。 */
    else if (c == 'N') { g_cross_min = (v > 0) ? clampi(v, 1, 8) : CFG_LINE_CROSS_MIN_ON; }
    /* R<0|1>: 装车朝向。⚠ 这一条**不能沿用"0=回默认"的约定** —— 0 在这里是一个合法值
     * (X1 在右), 若把它当"回默认"就永远发不出"X1 在右"这个指令。故 R 直接取 0/1 字面值。 */
    else if (c == 'R') { g_x1_left = (v != 0) ? 1 : 0; line_pos_apply(); }
    else return 0;
    /* HI 必须严格大于 LO, 否则下游插值分母为 0 / 负 ⇒ 直接除爆或算出反向速度。
     * 在**入口就夹死**而不是在控制回路里判: 回路每拍都跑, 把校验放那儿等于每拍付一次代价,
     * 而且真出错时车已经在跑了。这里顶多让用户看到一个被修正过的回显。 */
    if (g_vs_hi <= g_vs_lo + 1.0f) g_vs_hi = g_vs_lo + 1.0f;
    print_vseg();
    return 1;
}
#endif

/* ==== 串口命令 ==== */
/* 命令行缓冲: **每个来源一个**。有线口与无线口会同时来字节, 共用一个缓冲会把两边的
 * 半截命令拼成一条乱命令(比丢命令更坏 —— 它会"成功执行"一个谁都没发过的指令)。 */
static char cbuf[16];
static int  clen = 0;
#if CFG_ESP_UART_EN
static char ebuf[16];
static int  elen = 0;
static uint32_t g_cmd_rej = 0;     /* 被格式门拒掉的行数(遥测里 rej= 字段) */
static volatile uint32_t g_bridge_end = 0;   /* AT 桥接到期时刻(SysTick 拍); 0=不在桥接。见 bridge_pump() */
#endif

static int parse_int(const char *s, int n)
{
    int i = 0, sg = 1, v = 0;
    if (i < n && s[i] == '-') { sg = -1; i++; }
    for (; i < n; i++) { if (s[i] < '0' || s[i] > '9') break; v = v * 10 + (s[i] - '0'); }
    return sg * v;
}
static void print_servo(void);   /* 定义在下面(挨着 print_magnet); `?` 要把转向状态一并回读 */
static void print_status(void)
{
    int li = loop_index();
    uart_dbg_puts("[ctl] mode="); uart_dbg_puts(mode_name[g_mode]);
    uart_dbg_puts(" tgt=");       uart_dbg_put_int(g_target);
    if (li >= 0) {
        uart_dbg_puts(" Kp*1e3="); uart_dbg_put_int((int)(gkp[li] * 1000));
        uart_dbg_puts(" Ki*1e3="); uart_dbg_put_int((int)(gki[li] * 1000));
        uart_dbg_puts(" Kd*1e3="); uart_dbg_put_int((int)(gkd[li] * 1000));
    }
    uart_dbg_puts(" (PWM_CAP="); uart_dbg_put_int(PWM_CAP); uart_dbg_puts("%)");
    /* 两个死区前馈都回读: 它们是"整定完必须回填 config.h"的运行时值, 不打出来就会忘掉自己在试哪个 */
    uart_dbg_puts(" dz_pos="); uart_dbg_put_int(g_pwm_dz);
    uart_dbg_puts(" dz_drv="); uart_dbg_put_int(g_drv_dz);
#if CFG_ESP_UART_EN
    /* sinks=当前输出去向(1有线/2无线/3双发, `l<mask>` 改) | rej=被格式门拒掉的行数。
     * rej 是"门在干活"的唯一可观测证据: 接了 ESP 后每次 ESP 上电它应该跳 ~20(boot 日志行数)。
     * rej 一直是 0 且 ESP 刚上过电 => 要怀疑 RX 根本没接上(PB3), 而不是"没有垃圾进来"。 */
    uart_dbg_puts(" sinks="); uart_dbg_put_int((int)uart_dbg_get_sinks());
    uart_dbg_puts(" rej=");   uart_dbg_put_int((int)g_cmd_rej);
#endif
    uart_dbg_puts("\n");
    /* m8/m9 下把导航增益一并回读: p/i/d 在这两个模式里改的是 g_nav 而不是 gkp[](见 nav_gain_cmd),
     * 若不打出来, "我刚改的 Kp 到底进去了没"就无法确认。 */
    if (g_mode == MODE_NAV_S) {
        uart_dbg_puts("[nav] hdg Kp*1e3="); uart_dbg_put_int((int)(g_nav.kp_hdg * 1000));
        uart_dbg_puts(" Kd*1e3=");          uart_dbg_put_int((int)(g_nav.kd_hdg * 1000));
        uart_dbg_puts(" v_cruise=");        uart_dbg_put_int((int)g_nav.v_cruise);
        uart_dbg_puts(" tol_mm=");          uart_dbg_put_int((int)g_nav.tol_mm);
        uart_dbg_puts("\n");
    } else if (g_mode == MODE_NAV_T) {
        uart_dbg_puts("[nav] turn Kp*1e3="); uart_dbg_put_int((int)(g_nav.kp_turn * 1000));
        uart_dbg_puts(" Kd*1e3=");           uart_dbg_put_int((int)(g_nav.kd_turn * 1000));
        uart_dbg_puts(" w_max=");            uart_dbg_put_int((int)g_nav.turn_w_max);
        uart_dbg_puts(" tol_deg*10=");       uart_dbg_put_int((int)(g_nav.turn_tol_deg * 10));
        uart_dbg_puts("\n");
    }
    print_servo();   /* 转向是阿克曼底盘的第二个执行器, `?` 不该只报驱动不报转向 */
}

/* 里程/转角标定值的回读（命令 c / q 都会调它）。
 * 为什么要单独打一行: 这两个数是**唯一让"走 N mm / 转 N 度"有意义的东西**, 而它们默认是 0。
 * 开跑前一眼确认它们不是 0, 比事后分析"车为什么只走了几厘米"便宜得多。 */
static void print_cal(void)
{
    uart_dbg_puts("[nav] counts/mm*100=");  uart_dbg_put_int((int)(g_nav.counts_per_mm * 100));
    uart_dbg_puts(" counts/deg*100=");      uart_dbg_put_int((int)(g_nav.counts_per_deg * 100));
    uart_dbg_puts(g_nav.counts_per_mm > 0.0f ? " (mm OK)" : " (mm NOT CALIBRATED -> n<mm> 会被拒)");
    uart_dbg_puts("\n");
}

/* 电磁铁状态 + 线圈电流(命令 E 之后自动打)。
 * ⚠ 打出来的 mA 回答的是"线圈通电了没/断线没", **不是"吸住了没"**(见 magnet.h 文件头:
 *   电磁铁稳态电流 ≈ V/R, 与有没有吸住铁件基本无关)。"吸住了没"要靠光电传感器。 */
static void print_magnet(void)
{
    magnet_state_t ms = magnet_state();
    int32_t ma = magnet_current_ma();
    uart_dbg_puts("[mag] state=");
    uart_dbg_puts(ms == MAG_OFF ? "OFF" : (ms == MAG_PULL ? "PULL" : "HOLD"));
    uart_dbg_puts(" duty=");  uart_dbg_put_int(magnet_duty());
    uart_dbg_puts("% I=");    uart_dbg_put_int((int)ma);
    uart_dbg_puts("mA coil=");
    uart_dbg_puts((ms == MAG_OFF) ? "-" : (magnet_coil_ok(ma) ? "ENERGIZED" : "NO CURRENT?查接线/驱动"));
    uart_dbg_puts(" (电流只证通电, 不证吸住; 吸住了没要用光电)\n");
}

/* 视觉链健康度(命令 V)。**排障时先看这一行, 再怀疑控制**。
 * 四个计数把"视觉不工作"分成四种修法完全不同的故障:
 *   ok=0 且全 0        -> 一个字节都没进来: 查 TX/RX 有没有交叉、有没有共地、模块跑没跑
 *   overflow>0         -> 波特率不匹配(永远等不到换行)
 *   bad_csum>0         -> 线路噪声 / 发端校验算错
 *   bad_form>0         -> 发端格式写错(字段数/非数字/标识不对)
 *   ok>0 但 status=STALE -> 曾经通过, 现在掉线了 */
/* ==== VIS_UART(PB16) 物理层计数器 ====
 * 为什么需要它: uf_* 那套统计只在**字节已经进到解析器**之后才动, 所以 "ok=0 且全 0" 这一种读数
 * 同时对应三个完全不同的世界 —— 线没接上 / 接上了但电平·波特率错(收到的是垃圾) / 字节进来了但
 * 被 vision_grab 当非帧字节丢掉。真机上这三者的修法毫不相干(动线 / 换波特率 / 改代码), 而
 * 2026-07-31 那次 bring-up 恰好卡在这个岔口: MCU 侧注入全 PASS, 真相机零帧, 无从判断该动哪边。
 * 加两个物理层计数器就能一次分开:
 *   bytes=0 err=0  -> RX 脚上**一个边沿都没有**: K230 没跑 / TX 接到 PB15 了 / 没共地
 *   bytes=0 err>0  -> 有信号但**帧结构对不上**: 波特率不符 / 电平不对 / 接的是别的信号
 *   bytes>0        -> 物理层通了, 问题在协议层(看 bad_csum / bad_form / overflow)
 * tail 存最近 8 个原始字节: 波特率错时它是稳定的乱码, 一眼可辨(而 "$BP,1,+" 就说明全好)。
 * ⚠ 它只统计 **VIS_UART** ⇒ 从调试口注入的 $BP 不会让 bytes 涨, 这正是要的隔离性。
 * 只在 poll_uart 里写、只在 print_vision 里读, 单线程, 不需要临界区。 */
typedef struct {
    uint32_t bytes;      /* 该脚上成功收到的原始字节总数 */
    uint32_t err;        /* 出现 framing/parity/break/overrun 的次数 */
    uint32_t err_last;   /* 最近一次的错误位(原始 mask, 供细分) */
    uint8_t  tail[8];    /* 最近 8 个原始字节(环形) */
    uint8_t  tail_n;     /* 写指针; 缓冲满时它正好指向最旧那个 */
} vis_rx_stat_t;

/* 两路都监听, 两路都计数 —— 这不是冗余, 是为了不必猜线插在哪。
 *
 * 载板 §10.1 把相机分给 `PB4/PB5`(UART1, J2-4L/J2-5L), 而固件这边改到了 `PB15/PB16`(UART2,
 * J2-6R/J2-7R)。照板子文档接线的人一定接在 PB5 上, 而 PB16 上就永远 bytes=0 —— 2026-07-31 真机
 * 就卡在这个不一致上, 且从 MCU 侧完全看不出"线在隔壁那根脚"。
 * 恰好 UART1 是**配好了但全工程没有任何代码读它**的空外设(循迹早已全面改成 GPIO 直读 X1..X8,
 * `git grep LINE_UART_INST` 零命中) ⇒ 白拿一路接收, 没有任何冲突。
 * 于是两路都喂 vision_grab: 线插在哪根都能通, 且 `V` 会直接报出相机在哪根脚上。
 * ⚠ 若循迹模块**同时**插在 PB5 上, 它的字节也会进 vision_grab —— 无害: 非 '$' 开头一律丢弃。 */
static vis_rx_stat_t g_vis;         /* PB16 = UART2 = 固件设计位 */
static vis_rx_stat_t g_vis1;        /* PB5  = UART1 = 载板文档位(空闲外设, 白拿) */

/* 相机字节的环形缓冲: 生产者 = 5kHz SysTick(vis_drain_isr), 消费者 = 主循环(poll_uart)。
 * 为什么必须有它、为什么抽取放在 SysTick 而不是开 UART RX 中断 —— 完整的账在 vis_drain_isr
 * 上方那段(一帧 17 字节背靠背 1.5ms vs FIFO 只有 4 深, 以及 syscfg 里那条"RX 中断故意不开")。 */
#define VIS_RING_SZ 256u        /* 必须是 2 的幂; 420B/s 下够 ~600ms 主循环卡顿的余量 */
static volatile uint8_t  g_vring[VIS_RING_SZ];
static volatile uint16_t g_vr_head;    /* 只由 SysTick 写 */
static volatile uint16_t g_vr_tail;    /* 只由主循环写 */
static volatile uint32_t g_vr_drop;    /* 环满而丢弃的字节数; 正常应恒为 0 */

/* 打一路接收脚的物理层状态。tail 的 HEX+ASCII 双视图是关键: 波特率错时它是稳定的乱码, 一眼可辨,
 * 而 "$BP,1,+" 这种可读内容直接说明物理层与波特率都对、只剩协议层可查。 */
static void print_vis_pin(const char *tag, const vis_rx_stat_t *s)
{
    uart_dbg_puts("\n[vs] ");            uart_dbg_puts(tag);
    uart_dbg_puts(": bytes=");           uart_dbg_put_int((int)s->bytes);
    uart_dbg_puts(" err=");              uart_dbg_put_int((int)s->err);
    if (s->err) { uart_dbg_puts("(mask="); uart_dbg_put_int((int)s->err_last); uart_dbg_puts(")"); }
    if (s->bytes) {
        static const char hexd[] = "0123456789ABCDEF";
        /* 最近 8 字节, 按时间先后。够 8 个就从写指针处绕一圈, 不够就从 0 开始。 */
        uint32_t n = (s->bytes < 8u) ? s->bytes : 8u;
        uint32_t b0 = (s->bytes < 8u) ? 0u : s->tail_n;
        uart_dbg_puts(" tail=");
        for (uint32_t k = 0; k < n; k++) {
            uint8_t b = s->tail[(b0 + k) & 7u];
            uart_dbg_putc(hexd[(b >> 4) & 0xF]);
            uart_dbg_putc(hexd[b & 0xF]);
            uart_dbg_putc(' ');
        }
        uart_dbg_puts("| ascii='");
        for (uint32_t k = 0; k < n; k++) {
            uint8_t b = s->tail[(b0 + k) & 7u];
            uart_dbg_putc((b >= 0x20 && b < 0x7F) ? (char)b : '.');
        }
        uart_dbg_puts("'  <== 相机在这根脚上");
    }
    uart_dbg_puts("\n");
}

static void print_vision(void)
{
    uint32_t ms = g_st / ST_PER_MS;
    uf_status_t st = uf_status(&g_uf, ms);
    uart_dbg_puts("[vs] status=");
    uart_dbg_puts(st == UF_OK ? "OK" : (st == UF_NO_DATA ? "NO_DATA" :
                 (st == UF_STALE ? "STALE" : "NO_TARGET")));
    /* 计数 = 相机实例 + 注入实例之和。求和(而不是各打一行)是为了**保持输出格式不变** ——
     * ball_bringup / vis_watch 的正则都按单组数字写的, 而两路的统计合起来才是"视觉链健康度"。
     * 之所以能直接相加不重复: 注入帧解析成功时只搬 last/have_frame, 不碰 g_uf 的计数器。 */
    uart_dbg_puts(" ok=");        uart_dbg_put_int((int)(g_uf.n_ok        + g_uf_inj.n_ok));
    uart_dbg_puts(" bad_csum=");  uart_dbg_put_int((int)(g_uf.n_bad_csum  + g_uf_inj.n_bad_csum));
    uart_dbg_puts(" bad_form=");  uart_dbg_put_int((int)(g_uf.n_bad_form  + g_uf_inj.n_bad_form));
    uart_dbg_puts(" overflow=");  uart_dbg_put_int((int)(g_uf.n_overflow  + g_uf_inj.n_overflow));
    uart_dbg_puts(" | last id="); uart_dbg_put_int((int)g_uf.last.id);
    uart_dbg_puts(" cx=");        uart_dbg_put_int((int)g_uf.last.cx);
    uart_dbg_puts(" area=");      uart_dbg_put_int((int)g_uf.last.area);
    uart_dbg_puts(" age_ms=");    uart_dbg_put_int((int)(g_uf.have_frame ? (ms - g_uf.last.stamp_ms) : 0));

    /* ---- 物理层那两行: 上面全是 0 时, 只有它们能告诉你该动线还是动代码 ---- */
    print_vis_pin("pb16(UART2,J2-7R)", &g_vis);
    print_vis_pin("pb5 (UART1,J2-5L)", &g_vis1);
    /* ring_drop>0 = 主循环有 >600ms 的卡顿(SysTick 抽到了但没人取走) ⇒ 是**主循环**的问题,
     * 与接线和波特率无关。不打出来的话它会表现成"偶发丢帧"而查不到源头。 */
    uart_dbg_puts("[vs] ring_drop=");   uart_dbg_put_int((int)g_vr_drop);
    uart_dbg_puts(" (>0 = 主循环卡顿吃不下, 不是接线问题)\n");
    /* 判读直接写在输出里 —— 排障时人不该再去翻文档对表。 */
    if (g_vis.bytes == 0 && g_vis1.bytes == 0 && g_vis.err == 0 && g_vis1.err == 0)
        uart_dbg_puts("[vs] => 两根脚都一个边沿都没有: ①相机脚本没在跑(K230 停在 REPL 就是这样, 屏上看不出来)"
                      " ②线接到了别的脚 ③没共地。**先别怀疑固件**, 注入测试已证解析链是好的。\n");
    else if (g_vis.bytes == 0 && g_vis1.bytes == 0)
        uart_dbg_puts("[vs] => 有信号但帧结构对不上(只有错误没有字节): 波特率不符 / 电平不对 / 接的不是串口。\n");
    else if (g_uf.n_ok == 0)
        uart_dbg_puts("[vs] => 物理层已通(字节在进来), 问题在协议层: 看 tail 的 ascii 是不是 $BP,... "
                      "是乱码则波特率错; 是好字符则看 bad_csum/bad_form/overflow。\n");
    uart_dbg_puts("[vs] 自测(不用相机): 往本口发一行 $V,1,200,240,900*<异或校验> 然后 m10\n");
}

/* 舵机状态（命令 S / U / C 之后自动打）。
 * cal=NO 时打出来的 deg **不可信**（中位与每度微秒数都还是估计值），所以把 cal 标记
 * 和数值放在同一行 —— 防止有人截个图就拿这个角度去分析转向误差。 */
static void print_servo(void)
{
    const servo_cal_t *c = servo_cal();
    int us = servo_us();
    uart_dbg_puts("[srv] us=");      uart_dbg_put_int(us);
    if (us == 0) uart_dbg_puts("(limp,无脉冲)");
    uart_dbg_puts(" deg*10=");       uart_dbg_put_int((int)(servo_deg() * 10));
    uart_dbg_puts(" | center=");     uart_dbg_put_int(c->center_us);
    uart_dbg_puts(" range=");        uart_dbg_put_int(c->min_us);
    uart_dbg_puts("..");             uart_dbg_put_int(c->max_us);
    uart_dbg_puts(" max_deg=");      uart_dbg_put_int((int)c->max_deg);
    uart_dbg_puts(" sign=");         uart_dbg_put_int(c->sign);
    uart_dbg_puts(" ccinv=");        uart_dbg_put_int(servo_cc_invert());
    uart_dbg_puts(CFG_SERVO_CALIBRATED ? " cal=YES\n" : " cal=NO(deg 不可信, 见 config.h §7.9)\n");
}

/* ==== H题滚球平衡(m12) 的三个小helper ==== */

/* 把 config.h §7.12 的值灌进 g_ball。**这里是配置的唯一落点** —— ball.c 不 include config.h。
 * ⚠ 顺序: 必须先 ball_init()(它铺默认值 + 清运行态), 再逐个覆盖。 */
static void ball_apply_cfg(void)
{
    ball_init(&g_ball);
    g_ball.kp            = CFG_BALL_KP;
    g_ball.kd            = CFG_BALL_KD;
    g_ball.ki            = CFG_BALL_KI;
    g_ball.i_band_mm     = CFG_BALL_I_BAND_MM;
    g_ball.i_limit_deg   = CFG_BALL_I_LIMIT_DEG;
    g_ball.theta_max_deg = CFG_BALL_THETA_MAX;
    g_ball.x_soft_mm     = CFG_BALL_X_SOFT_MM;
    g_ball.x_hard_mm     = CFG_BALL_X_HARD_MM;
    g_ball.alpha         = CFG_BALL_ALPHA;
    g_ball.beta          = CFG_BALL_BETA;
    g_ball.use_model     = CFG_BALL_USE_MODEL;
    g_ball.max_age_s     = (float)CFG_BALL_MAX_AGE_MS / 1000.0f;
    g_ball.ff_ax_en      = CFG_BALL_FF_AX;
    g_ball.ff_pitch_en   = CFG_BALL_FF_PITCH;
    g_ball.traj_amp_mm   = CFG_BALL_TRAJ_AMP_MM;
    g_ball.traj_t_out    = CFG_BALL_TRAJ_T_OUT;
    g_ball.traj_t_dwell  = CFG_BALL_TRAJ_T_DWELL;
    g_ball.traj_t_back   = CFG_BALL_TRAJ_T_BACK;
    g_ball.traj_t_settle = CFG_BALL_TRAJ_T_SETTLE;
    g_ball_stamp = 0; g_ball_stamp_init = 0; g_ball_v_prev = 0.0f;
}

/* ball 给的是"摆杆对车"的倾角(度); 这里叠上装配标定后交给舵机层。
 * ⚠ 两级限幅: ball 内部已按 CFG_BALL_THETA_MAX 夹过, servo 层还会按它自己的 max_deg 再夹一次。
 * ⚠ 中位与符号都还是 ⬜ 待实测 ⇒ 摆杆装好后先按 config.h §7.12 那两个实验(S1/S2)定死再整定。 */
static void ball_drive_servo(float th_deg)
{
    servo_set_deg(CFG_BALL_SERVO_MID_DEG + (float)CFG_BALL_SERVO_SIGN * th_deg);
}

/* 滚球状态回读(命令 `?` 与 `R` 之后自动打)。整数化输出, 无 printf。
 * ⭐ 四个分量(pd/traj/ax/pitch)单独打出来的理由: 它们是"我们真的在补偿"的**可视化证据**
 *   (对应校赛B 把扰动估计 f_hat 画出来那招, 答辩加分点), 也是整定时判"哪一项在起作用"的唯一手段。
 * ⚠ peak 才是判分量 —— 官方 Q37「考察全程」⇒ 判分取最坏帧, 别看末态 err。 */
static void print_ball(void)
{
    static const char *st[] = { "IDLE", "HOLD", "TRAJ", "DONE", "BLOCKED" };
    uart_dbg_puts("[ball] st=");    uart_dbg_puts(st[(int)g_ball.state]);
    uart_dbg_puts(" fail=");        uart_dbg_puts(ball_fail_str(g_ball.fail));
    uart_dbg_puts(" warn=");        uart_dbg_puts(ball_fail_str(g_ball.warn));
    uart_dbg_puts(" | x*10=");      uart_dbg_put_int((int)(g_ball.x_est * 10.0f));
    uart_dbg_puts(" ref*10=");      uart_dbg_put_int((int)(g_ball.x_ref_mm * 10.0f));
    uart_dbg_puts(" v=");           uart_dbg_put_int((int)g_ball.v_est);
    uart_dbg_puts(" | err*10=");    uart_dbg_put_int((int)(g_ball.err_mm * 10.0f));
    uart_dbg_puts(" PEAK*10=");     uart_dbg_put_int((int)(g_ball.peak_abs_err_mm * 10.0f));
    uart_dbg_puts("\n[ball] th*10="); uart_dbg_put_int((int)(g_ball.theta_cmd_deg * 10.0f));
    uart_dbg_puts(" (pd=");         uart_dbg_put_int((int)(g_ball.th_pd_deg * 10.0f));
    uart_dbg_puts(" i=");           uart_dbg_put_int((int)(g_ball.th_i_deg * 10.0f));
    uart_dbg_puts(" traj=");        uart_dbg_put_int((int)(g_ball.th_traj_deg * 10.0f));
    uart_dbg_puts(" fric=");        uart_dbg_put_int((int)(g_ball.th_fric_deg * 10.0f));
    uart_dbg_puts(" ax=");          uart_dbg_put_int((int)(g_ball.th_ax_deg * 10.0f));
    uart_dbg_puts(" pit=");         uart_dbg_put_int((int)(g_ball.th_pitch_deg * 10.0f));
    uart_dbg_puts(") sat=");        uart_dbg_put_int(g_ball.sat);
    uart_dbg_puts(" nomeas=");      uart_dbg_put_int((int)g_ball.no_meas_ticks);
    uart_dbg_puts(" | kp*1000=");   uart_dbg_put_int((int)(g_ball.kp * 1000.0f));
    uart_dbg_puts(" kd*1000=");     uart_dbg_put_int((int)(g_ball.kd * 1000.0f));
    uart_dbg_puts(" ki*1000=");     uart_dbg_put_int((int)(g_ball.ki * 1000.0f));
    uart_dbg_puts(" iband*10=");    uart_dbg_put_int((int)(g_ball.i_band_mm * 10.0f));
    uart_dbg_puts(" ilim*100=");    uart_dbg_put_int((int)(g_ball.i_limit_deg * 100.0f));
    uart_dbg_puts(" ia=");          uart_dbg_put_int(g_ball.i_active);
    uart_dbg_puts(" aw=");          uart_dbg_put_int(g_ball.i_aw_hold);
    uart_dbg_puts(" ff=");          uart_dbg_put_int(g_ball.ff_ax_en | (g_ball.ff_pitch_en << 1));
    /* 这两个也必须能回读: 它们是**在线可改**的(M/F), 而改完不回读就等于不知道现在在跑什么。 */
    uart_dbg_puts(" thmax*10=");    uart_dbg_put_int((int)(g_ball.theta_max_deg * 10.0f));
    uart_dbg_puts(" a*100=");       uart_dbg_put_int((int)(g_ball.alpha * 100.0f));
    uart_dbg_puts(" b*100=");       uart_dbg_put_int((int)(g_ball.beta * 100.0f));
    if (g_ball.traj_phase > 0) {    /* 轨迹跑过才打这一行, 否则全是"未测到"的 -1 反而干扰判读 */
        uart_dbg_puts("\n[ball] traj ph=");   uart_dbg_put_int(g_ball.traj_phase);
        uart_dbg_puts(" t*10=");              uart_dbg_put_int((int)(g_ball.traj_total_s * 10.0f));
        uart_dbg_puts(" wpOUT*10=");          uart_dbg_put_int((int)(g_ball.err_wp_out_mm * 10.0f));
        uart_dbg_puts(" wpBACK*10=");         uart_dbg_put_int((int)(g_ball.err_wp_back_mm * 10.0f));
        uart_dbg_puts("  (门限 100=10mm; 自设目标 50=5mm)");
    }
    uart_dbg_puts("\n");
}

/*
 * 滚球层的命令。返回 1 = 已消费(调用方直接 return)。
 * 设计取舍: **只新增一个字母 `R`** —— 52 个字母里小写已全被占, 大写只剩 13 个。所以沿用
 *   m11 已经建立的"p/i/d/t 按模式路由"约定(SSOT §D 记着"i 被刻意复用"这个先例):
 *     m12 下  t<mm>     = 目标位置(带符号, 语义与"目标"一致)
 *             p<×1000>  = kp        d<×1000> = kd
 *             i<0..3>   = 前馈掩码(bit0=a_x, bit1=pitch)  —— 滚球是纯 PD 无 I, 这个位置空着
 *             M<deg>    = 倾角限幅(安全权限)     F<×100>  = 观测器 alpha(beta 自动同步)
 *     任意模式 P1 = 起动要求3 往返轨迹(会自动进 m12) | P0 = 中止回 HOLD
 *       🔁 2026-07-31 由 `R` 改为 `P`：`R` 被 vseg_cmd 拦在前面无条件吃掉(且会翻循迹朝向), 详见下方
 * 为什么 theta_max 与 alpha 也必须是在线的(而不是只放 config.h): 它们是整定滚球时改得最频繁的
 *   两个量 —— theta_max 决定"敢给多大权限"(从小往大放是唯一安全的顺序), alpha 决定 24Hz 反馈下
 *   微分噪声压不压得住。留在 config.h 就意味着每试一个值烧一次板, 直接撞禁忌 2(连续快烧已把这块
 *   MCU 怼进 lockup 过一次)。M/F 用大写是刻意的: 整定脚本发错一个字母不该变成"设了个电机目标"。
 * ⚠ 全部只活在 RAM ⇒ 整定出达标值必须回填 config.h §7.12 并 commit("达标即锁死")。
 */
static int ball_cmd(char c, int v)
{
    /* 🔴 起动轨迹用 `P`（Profile），**不能用 `R`** —— 2026-07-31 真机踩实的字母碰撞：
     *   `run_cmd` 里那行 `if (c=='A'||'H'||'D'||'N'||'R') { if (vseg_cmd(c,v)) return; }` 拦在
     *   `ball_cmd` **之前**，而 `vseg_cmd` 对 `R` 是**无条件**处理（设循迹 X1 朝向后 return 1）
     *   ⇒ `R1` 永远到不了这里。实测判据：发 `R1` 只回 `[vseg] ... x1left=1`，没有 `[ball] TRAJ start`。
     *   那行注释自己早写了这个代价（"一旦撞了…会被静默吃掉"），这次就是被它言中。
     * ⚠ 更坏的是它**有副作用**：`R1` 会把 `g_x1_left` 翻成 1（本车实测是 X1 在**右**、`pos0=-42`）
     *   ⇒ 误发 `R1` 等于把循迹探头朝向翻反、循迹会朝反方向跑飞。已用 `R0` 恢复。
     * ⇒ 故本层改用 `P0/P1`；`R` 不再在这里出现，避免"看代码以为能用"。
     * 空闲大写只剩 `I J O P Z`（`A H D N R` 归 vseg，`M F` 已被本层占）；选 P 而非 I/O 是因为
     *   后两者在串口日志里与 1/0 极易看错，而这是个会让车动起来的命令。 */
    if (c == 'P') {
        if (v > 0) {
            /* 起动轨迹前**先把车级指令清零**: 要求3 明文"小车在静止状态时" */
            g_dv = 0; g_dw = 0; g_target = 0;
            g_mode = MODE_BALL;
            ball_start_traj(&g_ball, 0.0f);   /* 0 = 用 config 的 ±50mm */
            uart_dbg_puts("\n[ball] TRAJ start: O->+");
            uart_dbg_put_int((int)g_ball.traj_amp_mm);
            uart_dbg_puts("mm -> 折返 -> -");
            uart_dbg_put_int((int)g_ball.traj_amp_mm);
            uart_dbg_puts("mm, 预计 t*10=");
            uart_dbg_put_int((int)(ball_traj_duration(&g_ball) * 10.0f));
            uart_dbg_puts(" (要求3 限 50)\n");
        } else {
            ball_set_hold(&g_ball, 0.0f);
            uart_dbg_puts("\n[ball] TRAJ abort -> HOLD 0mm\n");
        }
        print_ball();
        return 1;
    }
    if (g_mode != MODE_BALL) return 0;
    switch (c) {
        case 't': ball_set_hold(&g_ball, (float)v); ball_reset_stats(&g_ball);
                  uart_dbg_puts("[ball] setpoint(mm)="); uart_dbg_put_int((int)g_ball.setpoint_mm);
                  uart_dbg_puts("\n"); return 1;
        case 'p': if (v > 0) g_ball.kp = (float)v / 1000.0f; print_ball(); return 1;
        case 'd': if (v >= 0) g_ball.kd = (float)v / 1000.0f; print_ball(); return 1;
        case 'i': g_ball.ff_ax_en    = (v & 1) ? 1 : 0;
                  g_ball.ff_pitch_en = (v & 2) ? 1 : 0;
                  uart_dbg_puts("[ball] ff ax="); uart_dbg_put_int(g_ball.ff_ax_en);
                  uart_dbg_puts(" pitch=");       uart_dbg_put_int(g_ball.ff_pitch_en);
                  uart_dbg_puts("  (做 A/B 量'前馈值多少毫米'用; 正式跑必须都=1)\n");
                  return 1;
        /* M<deg> = 倾角限幅。**整定第一天必须先把它调小**(从 2~3° 起), 理由:
         * 摆杆一动球就加速, theta_max 就是"这个回路能对球施加多大权限"。默认 6° 对应
         * a_max=0.73m/s², 在一根 25cm 管里已经很凶 —— 符号一旦搞反, 球会直接冲到端点挡片。
         * 硬顶 BALL_THETA_MECH_MAX_DEG(11.54°) 来自题目 h>=5cm 的几何, 不是设计选择 ⇒ 不许超。 */
        case 'M': if (v >= 1 && v <= (int)BALL_THETA_MECH_MAX_DEG) g_ball.theta_max_deg = (float)v;
                  else uart_dbg_puts("[ball] M<1..11> 度; 上限是题目 h>=5cm 给的机械顶 11.54\n");
                  print_ball(); return 1;
        /* F<alpha*100> = 观测器位置修正增益。24Hz 相机 + 50Hz 控制 ⇒ 约 1/3 的拍没有新测量,
         * 微分噪声全靠这个观测器压。alpha 小=平滑但滞后大(相位滞后会吃掉 kd 的阻尼),
         * alpha 大=跟得紧但把量化噪声放进 v_est。
         * ⚠ beta 必须跟着一起动: 经典临界阻尼取 beta=alpha^2/(2-alpha)。只改 alpha 不改 beta,
         *   观测器就成了一对不自洽的增益 —— 这类"改了一半"的错最难看出来, 所以在这里一并算掉。 */
        /* G<度×100> = 摩擦(起动阻力)前馈幅值。0 = 关闭。
         * 为什么必须是在线的: 它应当等于**实测起动阈值**，而那个量随管子清洁度与位置变化极大
         *   (2026-07-31 实测 0.53~1.59°，3 倍散布) ⇒ 必须现场扫，纸面值没有意义。
         * ⚠ 上限 300 = 3.00°: 再大就超过弱侧总权限(1.67°)，只会让输出恒饱和。 */
        /* J<deg x100> = friction (breakaway) feedforward magnitude, 0 = off.
         * Must be an ONLINE knob: its right value is the measured breakaway angle, and that varies
         * hugely with tube cleanliness and position (measured 2026-07-31: 0.53..1.59 deg, a 3x spread),
         * so no compile-time default is meaningful. Cap 300 = 3.00 deg; beyond the weak side's 1.67 deg
         * of authority it would only pin the output at saturation.
         * Letter choice: 'G' is already the line-follower field calibration (G0/G1/G2). ball_cmd only
         * sees letters while m12 is active so they would not actually collide, but that is exactly the
         * silent-shadowing trap documented above - so use a letter with zero existing use. 'I'/'O' read
         * as 1/0 in serial logs and 'Z' sits next to the emergency stop 'z', hence 'J'. */
        case 'J': if (v >= 0 && v <= 300) g_ball.fric_deg = (float)v / 100.0f;
                  else uart_dbg_puts("[ball] J<0..300> = friction ff, deg x100 (0=off)\n");
                  print_ball(); return 1;
        /* I<Ki*1000> = weak integral gain. Changing Ki always clears accumulated state so an A/B run
         * changes exactly one variable instead of inheriting history from the previous setting. */
        case 'I': if (v >= 0 && v <= 10000) {
                      g_ball.ki = (float)v / 1000.0f;
                      ball_reset_integral(&g_ball);
                  } else uart_dbg_puts("[ball] I<0..10000> = Ki*1000 (0=off, resets integrator)\n");
                  print_ball(); return 1;
        case 'F': if (v >= 1 && v <= 99) {
                      float a = (float)v / 100.0f;
                      g_ball.alpha = a;
                      g_ball.beta  = a * a / (2.0f - a);
                  } else uart_dbg_puts("[ball] F<1..99> = alpha*100 (beta 自动按 a^2/(2-a) 同步)\n");
                  print_ball(); return 1;
        default:  return 0;
    }
}

/* 导航任务结束时的"成绩单" —— 一行讲完这趟到底干成什么样。
 * 为什么必须自动打、而不是等人来问: 脱缆落地时串口线不在车上, 无线是 UDP 会丢, 人也不在电脑前;
 * 这一行是这趟唯一的定量记录。字段全部整数(无 printf): deg 与 mm 各自的倍率写在字段名里。 */
#if CFG_LINE_UART_EN
/* 任务层成绩单/状态回读。
 * ⭐ 这一行是**第 2 项的自查依据**：走时(内部) + 里程 + 丢线段数 + 到底为什么停。
 * ⚠ Q47 明确"屏显信息仅为参考、以评委秒表为准" ⇒ 这个走时只用于我们自查与整定,
 *   别拿它去跟评委争成绩。 */
static void print_task(void)
{
    static const char *ST[] = { "IDLE", "RUN", "BRAKE", "DONE", "ABORT" };
    static const char *FL[] = { "-", "TIMEOUT", "LOST", "MANUAL" };
    int st = (g_task.state >= 0 && g_task.state <= 4) ? g_task.state : 0;
    int fl = (g_task.fail  >= 0 && g_task.fail  <= 3) ? g_task.fail  : 0;
    char tb[12];
    task_fmt_time(g_task.state == TASK_IDLE ? 0u
                  : (g_task.state == TASK_DONE || g_task.state == TASK_ABORT)
                        ? (g_task.t_stop - g_task.t_start)
                        : (g_st / ST_PER_MS - g_task.t_start), tb, sizeof tb);
    uart_dbg_puts("[task] "); uart_dbg_puts(ST[st]);
    uart_dbg_puts(" t=");     uart_dbg_puts(tb);
    uart_dbg_puts("s run#");  uart_dbg_put_int((int)g_task.n_runs);
    uart_dbg_puts(" fail=");  uart_dbg_puts(FL[fl]);
    uart_dbg_puts(" | line st=");  uart_dbg_put_int(g_line_st);
    uart_dbg_puts("(0OK/1LOST/2CROSS/3NOCAL) err0.1mm=");
    uart_dbg_put_int((int)(g_line_err * 10.0f));
    uart_dbg_puts(" cal=");   uart_dbg_puts(line_calibrated(&g_line) ? "YES" : "NO");
    uart_dbg_puts(" lostSeg="); uart_dbg_put_int((int)g_lost_seg);
    /* on=本拍在线路数/本趟最大 —— 启停线门限(CFG_LINE_CROSS_MIN_ON)该不该改就看 max:
     * max 从未到过门限 ⇒ 探头离地太高或胶带太窄, 调高度或降门限; max 轻易超门限 ⇒ 有误触风险。 */
    uart_dbg_puts(" on=");    uart_dbg_put_int(g_cross_cur);
    uart_dbg_puts("/");       uart_dbg_put_int(g_cross_max);
    uart_dbg_puts(" xrun=");  uart_dbg_put_int(g_cross_run);
    /* xcnt = 本趟压到启停线的次数(上升沿)。**验收时先看它**:
     * 0 ⇒ 判据没触发, 去查 on=max 够不够门限; >=1 却没自停 ⇒ 是 task 层门限挡的, 别去动探头。 */
    uart_dbg_puts(" xcnt=");  uart_dbg_put_int((int)g_cross_cnt);
    /* wlp=曲率代理, vseg=分段速度本拍给的 RPM ⇒ 一眼看出"弯道有没有真的降速、降到多少"。
     * 没有这两个数就只能靠猜分段速度是否在工作(遥测里的 V 是实测转速、含速度环滞后)。 */
    uart_dbg_puts(" wlp=");   uart_dbg_put_int((int)g_w_lp);
    uart_dbg_puts(" vseg=");  uart_dbg_put_int(g_vseg_now);
    uart_dbg_puts(" D="); uart_dbg_put_int((int)g_lf.n_dig);
    uart_dbg_puts(" bad="); uart_dbg_put_int((int)g_lf.n_bad);
    uart_dbg_puts("\n");
    print_vseg();   /* 跟着打分段速度三个值 —— 整定时"我现在设的是多少"必须能一眼回读 */
}
#endif

static void nav_report(void)
{
    uart_dbg_puts("\n[nav] ");
    uart_dbg_puts(g_nav.state == NAV_DONE ? "DONE " : "STOP ");
    uart_dbg_puts(g_nav.mode == NAV_M_TURN ? "TURN" : "STRAIGHT");
    if (g_nav.mode == NAV_M_TURN) {
        uart_dbg_puts(" tgt_deg*10=");  uart_dbg_put_int((int)(g_nav.tgt_deg * 10));
        uart_dbg_puts(" done_deg*10="); uart_dbg_put_int((int)(g_nav.done_deg * 10));
        uart_dbg_puts(" err_deg*10=");  uart_dbg_put_int((int)(g_nav.err_deg * 10));
    } else {
        uart_dbg_puts(" tgt_mm=");      uart_dbg_put_int((int)g_nav.tgt_mm);
        uart_dbg_puts(" done_mm=");     uart_dbg_put_int((int)g_nav.done_mm);
        uart_dbg_puts(" err_mm=");      uart_dbg_put_int((int)g_nav.err_mm);
        uart_dbg_puts(" peak_hdg_deg*10="); uart_dbg_put_int((int)(g_nav.peak_hdg_deg * 10));
    }
    /* 航向来源必须如实说: GYRO(陀螺, 首选) / ENC(编码器兜底, 靠"不打滑"这个假设) / NONE(没纠偏)。
     * 一趟 NONE 的走直即使走得很直, 也**不是航向环的功劳**, 不能当成阶梯 3 达标。 */
    uart_dbg_puts(" | hdg=");
    uart_dbg_puts(g_nav.hdg_used == 1 ? "GYRO" : (g_nav.hdg_used == 2 ? "ENC" : "NONE"));
    if (g_nav.state == NAV_BLOCKED) { uart_dbg_puts(" | FAIL="); uart_dbg_puts(nav_fail_str(g_nav.fail)); }
    if (g_nav.warn != NAV_F_NONE)   { uart_dbg_puts(" | WARN="); uart_dbg_puts(nav_fail_str(g_nav.warn)); }
    uart_dbg_puts("\n");
}
/* IMU 验活读数(命令 'g'): 任何时候 5 秒问清"IMU 到底通不通"的常备命令。
 * 打印 WHO_AM_I(应=71=0x47) + 陀螺(0.01°/s) + 加速度(mg) + 温度(0.1℃) + |a| 与定轴提示。
 * 无芯片/接线断时 WHOAMI 读 0 或 255 -> 查 CS(PB6)/MISO(PB7)接线与供电。
 * 真机验活通过 2026-07-27(commit a808122): WHOAMI=0x47 / 静止陀螺≤0.25dps / |a|=0.995g / 29.5℃。
 * ⚠ 四个判据要连起来看: ID 对只证总线通, **|a|≈1000mg 才证量程/LSB 定标对**(最便宜的自检)。 */
static void imu_dump(void)
{
    uint8_t id = imu_whoami();
    uart_dbg_puts("[imu] WHOAMI="); uart_dbg_put_int((int)id);
    if (id == ICM42688_WHOAMI_VAL) {
        imu_raw_t r; float gd[3], ag[3];
        imu_read_raw(&r);
        imu_convert(&r, gd, ag);
        uart_dbg_puts(" OK(0x47)\n[imu] gyro cdps:");
        uart_dbg_put_int((int)(gd[0]*100)); uart_dbg_putc(','); uart_dbg_put_int((int)(gd[1]*100)); uart_dbg_putc(','); uart_dbg_put_int((int)(gd[2]*100));
        uart_dbg_puts(" | accel mg:");
        uart_dbg_put_int((int)(ag[0]*1000)); uart_dbg_putc(','); uart_dbg_put_int((int)(ag[1]*1000)); uart_dbg_putc(','); uart_dbg_put_int((int)(ag[2]*1000));
        uart_dbg_puts(" | temp0.1C:"); uart_dbg_put_int((int)(imu_temp_c(r.temp)*10));
        uart_dbg_puts("\n");
        /* L1 定轴用: 直接打出"加速度模长"和"每轴 mg" —— 平放时哪个轴 ≈±1000mg 就是竖直轴。
         * 模长 ≈1000mg 同时也是加速度计定标(±4g/8192LSB/g)的自检。 */
        {
            float mag = ag[0]*ag[0] + ag[1]*ag[1] + ag[2]*ag[2];
            /* 简易 sqrt(牛顿迭代 3 次, 够打印精度; 避免再引一次 sqrtf 依赖) */
            float s = (mag > 0.0f) ? mag : 0.0f, q = s > 1.0f ? s : 1.0f;
            for (int it = 0; it < 8; it++) q = 0.5f * (q + s / q);
            uart_dbg_puts("[imu] |a|mg="); uart_dbg_put_int((int)(q*1000));
            uart_dbg_puts(" (平放时该值≈1000, 且某一轴≈±1000 => 那轴是竖直轴 -> 填 CFG_YAW_AXIS)\n");
            uart_dbg_puts("[imu] yaw0.1deg="); uart_dbg_put_int((int)(g_att.yaw*10));
            uart_dbg_puts(" axis=");          uart_dbg_put_int(g_yaw_axis);
            uart_dbg_puts(" sign=");          uart_dbg_put_int(g_yaw_sign);
            uart_dbg_puts(" bias0.01dps=");   uart_dbg_put_int((int)(g_att.gbias[2]*100));
            uart_dbg_puts(" (a<0|1|2>改轴 s<1|-1>改符号, 改完必须重新 k 标定)\n");
            /* ⭐ pitch/roll 必须打出来 —— 在 2026-07-31 之前它们**全工程一处都没被打印过**,
             * g_att.pitch 只喂给 ball.c 和 LCD 水平仪页。后果是作战地图 A7 那条判决实验
             * ("静止读 pitch 噪声 <0.1deg; 手垫车一侧看 pitch 跟不跟随、符号对不对")
             * **物理上无法执行** —— 而 A7 是第 5+6 项共 40 分的前置假设。
             * 三个用途: ① 定 CFG_BALL_PITCH_SIGN(垫车看符号) ② 验 A7 噪声门限
             *   ③ 弯道球偏时判"是不是 pitch 在作妖"(审题 L2.4 列的头号嫌疑)。
             * ⚠ 打的是 g_att.pitch **原值**, 不含 CFG_BALL_PITCH_SIGN —— 定符号时必须看未经该符号
             *   加工的量, 否则是循环论证(拿待定的符号去解释现象)。m12 遥测里 print_ball 的 `pit=`
             *   是**乘过符号且取过负**的前馈贡献量, 不能拿它定符号。 */
            uart_dbg_puts("[imu] pitch0.1deg="); uart_dbg_put_int((int)(g_att.pitch*10));
            uart_dbg_puts(" roll0.1deg=");       uart_dbg_put_int((int)(g_att.roll*10));
            uart_dbg_puts(" (原值, 未乘 CFG_BALL_PITCH_SIGN)\n");
            uart_dbg_puts("[imu] A7 判据: 静止连读多次 pitch 抖动应 <1(=0.1deg); 垫高车头看 pitch 是否单向跟随\n");
        }
    } else {
        uart_dbg_puts(" (期望71=0x47; 读到0或255=无响应,查CS/MISO接线与供电)\n");
    }
}

/* 改了轴/符号后必须调用: 旧零偏是在旧坐标系里测的, 留着会把错轴的零偏积分进 yaw
 * (症状=静止也慢慢转, 极易被误判成"陀螺漂移大"而去乱加死区)。清零 + 明确提示重标。 */
static void yaw_frame_changed(const char *what)
{
    g_cal_left = 0;
    g_up_valid = 0;                       /* 天顶向量也是在旧坐标/旧姿态下测的 -> 一并作废 */
    g_gb_raw[0] = g_gb_raw[1] = g_gb_raw[2] = 0.0f;
    g_att.gbias[0] = g_att.gbias[1] = g_att.gbias[2] = 0.0f;
    attitude_reset_yaw(&g_att, 0.0f);
    uart_dbg_puts("[imu] "); uart_dbg_puts(what);
    uart_dbg_puts(" changed -> axis="); uart_dbg_put_int(g_yaw_axis);
    uart_dbg_puts(" sign=");            uart_dbg_put_int(g_yaw_sign);
    uart_dbg_puts("; bias CLEARED, yaw=0 -> 静止后重发 k 标定\n");
}

static void run_cmd(const char *s, int n)
{
    char c = s[0];
    int v = parse_int(s + 1, n - 1), li;
    g_cmd_at = g_st;                 /* 任何命令都刷新静默超时(为什么是"任何"见 config.h §7 取舍) */
    int mode_before = g_mode;
#if CFG_LINE_UART_EN
    /* A/H/D/N/R 五条循迹旋钮先拦一手。放在 switch 前面而不是加五个 case:
     * 它们**不分模式都该能改**(整定时常在 m11 外先设好再起跑), 而 switch 里
     * 已有的模式相关分支容易把这层语义搞乱。
     * ⚠ **代价**: 拦在前面 ⇒ 一旦选的字母与 switch 里已有的 case 撞了, 那条老命令会被
     *   **静默吃掉**(没有报错、没有回显)。所以加新字母前必须查一次占用表, 查法见 vseg_cmd 注释。 */
    if (c == 'A' || c == 'H' || c == 'D' || c == 'N' || c == 'R') { if (vseg_cmd(c, v)) return; }
#endif
    /* 滚球层先看一眼: m12 下 t/p/d/i/M/F 归它(同 m11 的路由约定), `P` 任何模式都收。
     * 放在 switch 前而不是加 case, 理由同上面那三条: 免得和 switch 里已有的模式分支纠缠。
     * ⚠ 注意它排在 vseg 那一行**之后** ⇒ `A H D N R` 这五个字母滚球层永远看不到。轨迹命令原本用 `R`,
     *   就是被这条顺序静默吃掉的(2026-07-31 真机), 已改用 `P`。**以后给滚球层加字母必须先查这行。** */
    if (ball_cmd(c, v)) return;
    switch (c) {
        case 'm': if (v >= 0 && v < MODE_N) { g_mode = v; g_target = 0; g_m1duty = 0; g_m2duty = 0; g_dv = 0; g_dw = 0; reset_all_pid();
                      /* 进 m12 就立刻开始护球(HOLD 0mm)。**这不是顺手** —— 1cm 钢球放在光滑无摩擦
                       * 的半圆槽里, 人一松手就滚走(题目禁止任何增摩擦改造) ⇒ 必须"按键之前就在闭环",
                       * 人才放得上去。官方 Q50「钢球脱落即判定本次失败」让这条从建议变成必须。 */
                      if (g_mode == MODE_BALL) { ball_set_hold(&g_ball, 0.0f); ball_reset_stats(&g_ball); print_ball(); }
                  } break;
        /* t<v>: 速度环目标。**在 m11 下改的是循迹巡航速度**(见 g_line_v_cruise) ——
         * 语义一致(都是"目标速度"), 且 m11 下 g_target 本来无用。
         * 为什么必须能在线改: 2026-07-29 真机证明**速度是循迹的决定性旋钮**而不是 Kp ——
         * 控制周期 50ms @250mm/s ⇒ 两拍间走 12.5mm = 一个探头间距, 叠上速度环上升时间后
         * 等效延迟折算约 40~60mm, 与阵列半宽 42mm 同量级 ⇒ 加 Kp 只会振荡(Kp 3.0 实测更差:
         * err 冲到 −30mm、w 饱和、830mm 就丢线, 而 Kp 2.0 能跑 1775mm)。 */
        case 't':
#if CFG_LINE_UART_EN
                  if (g_mode == MODE_LINE) {
                      if (v > 0 && v <= 200) { g_line_v_cruise = v;
                          uart_dbg_puts("[line] cruise(RPM)="); uart_dbg_put_int(g_line_v_cruise); }
                      else { g_line_v_cruise = 0;
                          uart_dbg_puts("[line] cruise 回默认 CFG_TASK_V_CRUISE="); uart_dbg_put_int(CFG_TASK_V_CRUISE); }
                      uart_dbg_puts("\n");
                      return;
                  }
#endif
                  g_target = v; reset_all_pid(); break;
        /* p/i/d 先给导航层一次机会(m8/m9 下增益住在 g_nav), 没被吃掉才走 gkp[] 那条老路 */
        case 'p': if (nav_gain_cmd('p', v)) break;
                  li = loop_index(); if (li >= 0) { gkp[li] = v / 1000.0f; apply_gains(); } break;
        case 'i': if (nav_gain_cmd('i', v)) break;
                  li = loop_index(); if (li >= 0) { gki[li] = v / 1000.0f; apply_gains(); } break;
        case 'd': if (nav_gain_cmd('d', v)) break;
                  li = loop_index(); if (li >= 0) { gkd[li] = v / 1000.0f; apply_gains(); } break;
        /* ---- 车级导航(阶梯 2.5/3/4) ----
         * n<mm>  走直 N 毫米(负=倒车), 全程保持发命令那一刻的航向
         * j<deg> 原地转 N 度(正=左转), 相对发命令那一刻的航向
         * 两条都是"发一次就自己跑完": 进对应模式 + 用当前计数/航向做基准起跑。
         * ⚠ 起跑基准就是**此刻**的姿态 ⇒ 车要先摆正; 陀螺没做过 `k` 标定时航向不可信,
         *   走直会退化成不纠偏(成绩单里会打 WARN=NO_HDG), 转角会直接拒(FAIL=NO_HDG)。 */
        case 'n': g_mode = MODE_NAV_S; reset_all_pid();
                  nav_start_straight(&g_nav, (float)v, encoder_count(ENC_1), encoder_count(ENC_2), g_att.yaw);
                  uart_dbg_puts("[nav] straight "); uart_dbg_put_int(v); uart_dbg_puts("mm start\n");
                  break;
        case 'j': g_mode = MODE_NAV_T; reset_all_pid();
                  nav_start_turn(&g_nav, (float)v, encoder_count(ENC_1), encoder_count(ENC_2), g_att.yaw);
                  uart_dbg_puts("[nav] turn "); uart_dbg_put_int(v); uart_dbg_puts("deg start\n");
                  break;
        /* c<x100> 里程标定 counts/mm ×100 (c392 => 3.92) | q<x100> 转角标定 counts/deg ×100
         * 参数为 0 时只回读不修改。**运行时可改的理由**: 标定要"跑一趟→量→算→再跑一趟"迭代好几轮,
         * 若每轮都得改 config.h 重烧, 就正撞"连续快烧"这个把本芯片怼进 lockup 的禁忌(SSOT §D2)。
         * ⚠ 只活在 RAM, 断电即失 ⇒ **标定值定下来必须回填 config.h 并 commit**("达标即锁死")。 */
        case 'c': if (v > 0) g_nav.counts_per_mm  = v / 100.0f;  print_cal(); return;
        case 'q': if (v > 0) g_nav.counts_per_deg = v / 100.0f;  print_cal(); return;
        /* ---- 电磁铁(阶梯 7) ----
         * E0 = 放(断电) | E1 = 吸(满占空 CFG_MAG_PULL_MS 后自动降到保持占空)
         * E<2..100> = 直接给这个占空(调吸力用, 不再自动降额 ⇒ 长时间用要自己盯着热)
         * ⚠ 大写 E 是有意的: 小写全被占了, 而 `e` 已经是位置环到位容差。
         * ⚠ 电流读数只证明"通电了", **不证明"吸住了"** —— 见 magnet.h 文件头。 */
        case 'E': if (v <= 0)      magnet_off();
                  else if (v == 1) magnet_on();
                  else             magnet_set(v);
                  print_magnet();
                  return;
        /* ---- 转向舵机(阿克曼前轮转向) ----
         * U<us> = 原始脉宽 us, U0 = 停脉冲(limp)。**标定专用**, 会被 config.h 的硬限幅夹。
         * S<deg> = 按转向角(正=左 或按 CFG_SERVO_SIGN)。S0 = 回正。两级限幅见 servo.h。
         * C<us>  = 在线设中位; **C0 = 把当前脉宽认作中位**(用 U 扫到前轮正对前方后一键锁定)。
         * ⚠ 三个都是大写: 小写 u(LCD 切页)/s(yaw 符号)/c(counts_per_mm) 全被占了。
         * ⚠ 中位在线定完必须回填 config.h §7.9 —— 只活在 RAM, 断电即失(同 `a`/`s` 的理由)。
         * ⚠ 真机第一次用请按 servo.h 文件末的 bring-up 顺序: **先摘掉转向拉杆**再发 U1500,
         *   那一步在两种 CC 极性假设下都不会撞机械限位。 */
        /* Y<0|1> = CC 极性(0=CC即高电平ticks / 1=反相)。2026-07-27 示波器发现 PA31 恒高 ⇒ 极性存疑,
         * 做成运行时可切以便一次烧录试完两种(重烧 115s + "连续快烧"禁忌)。定下来回填 config.h。 */
        case 'Y': servo_set_cc_invert(v); print_servo(); return;
        case 'U': servo_write_us(v); print_servo(); return;
        case 'S': servo_set_deg((float)v); print_servo(); return;
        case 'C': if (!servo_set_center_us(v))
                      uart_dbg_puts("[srv] 设中位被拒: C0 需要当前有脉宽(先用 U 扫), 或该值超出硬限幅\n");
                  print_servo();
                  return;
        /* V = 视觉链健康度(帧计数 + 最近一帧 + 新鲜度)。排障先看它再怀疑控制。 */
        case 'V': print_vision(); return;
        /* ---- 循迹模块串口(UART1=PB4/PB5) 的两个嗅探命令。协议未知, 先看字节再谈解析。 ----
         * L        = dump 统计 + HEX/ASCII 双视图('|' 标帧间隙 ⇒ 帧边界直接可见)
         * B<baud>  = 运行时换波特率并清缓冲。**一次烧录扫完所有候选**, 不必为试速率重烧
         *            (禁忌 2: 反复快烧会把 MCU 怼进 lockup, 已发生过一次)。
         * 定出真值后回填 config.h 的 CFG_LINE_UART_BAUD 再 commit（"达标即锁死"）。 */
        case 'L': linesens_dump(); return;
        case 'B': if (v >= 1200 && v <= 460800) { linesens_set_baud((uint32_t)v); linesens_dump(); }
                  else uart_dbg_puts("[line] B<baud> 超范围(1200..460800); 常见候选 9600/19200/38400/57600/115200\n");
                  return;
        /* T = UART1 内部回环自测。回答那个最要紧的分岔口:
         *   "收不到"是**我们的接收链路坏了**, 还是**对方根本没发**? 见 linesens.h 里的说明。*/
        case 'T': line_selftest(); return;
        /* X<0..255> = 往模块发一个字节(十进制)。协议未知时用来试查询帧。 */
        case 'X': if (v >= 0 && v <= 255) { linesens_clear(); linesens_tx((uint8_t)v);
                      line_wait_ms(60);
                      uart_dbg_puts("[line] sent 0x"); uart_dbg_put_int(v);
                      uart_dbg_puts(" -> rx="); uart_dbg_put_int((int)linesens_rx_total());
                      uart_dbg_puts("\n"); linesens_dump(); }
                  else uart_dbg_puts("[line] X<0..255>\n");
                  return;
        /* Q = **查询字节全扫**(0..255 逐个发, 看哪个能把模块问出话来)。
         * 这是"没有任何文档也能推进"的手段: 若模块是问答式, 命中的那个字节就是查询帧的第一字节。
         * ⚠ 有代价: 盲发可能撞上模块的配置/学习命令, 把出厂阈值改掉。该模块有实体按键可重新学习,
         *   所以后果可恢复 —— 但**这属于会改对方状态的操作, 必须用户明确同意后才跑**。 */
        case 'Q': line_sweep_query(); return;
        /* 急停 = "忘掉一切"(与超时自停共用 stop_all, 见其注释里那个真机踩过的坑) */
        case 'z': stop_all(); break;
        /* h<ms>: 临时放宽/收紧静默超时(0=恢复 config.h 按模式默认)。
         * 用途: "不发命令但要持续观察"的测试(如位置环手推刚度)先发 h10000, 免得中途被踢回 IDLE。
         * 注意它**不能**绕过硬上限 CFG_RUN_MS_HARDCAP —— 那是最后一道墙, 有意不给运行时开关。 */
        case 'h': if (v == 0) g_run_ms_ovr = 0;
                  else if (v >= 200 && v <= 20000) g_run_ms_ovr = v;
                  uart_dbg_puts("[ctl] run timeout = ");
                  uart_dbg_put_int((int)run_limit_ms(g_mode));
                  uart_dbg_puts(g_run_ms_ovr > 0 ? "ms (override)\n" : "ms (per-mode default)\n");
                  break;
        case 'f': if (v >= 5 && v <= 2000) g_print_ms = v; break;   /* 遥测周期 ms(整定抓暂态用) */
        case 'w': if (v >= 0 && v <= 30)  g_pwm_dz  = v; break;     /* 位置死区前馈% */
        /* W<%>: 速度环死区前馈(差速/转角/导航共用)。0=关(旧行为)。大写, 小写 w 是位置环那个。
         * 整定完必须回填 config.h 的 CFG_DRV_FF_DZ, 否则断电即失。机理见 drive_closed_loop 注释。 */
        case 'W': if (v >= 0 && v <= 30) { g_drv_dz = v;
                      uart_dbg_puts("[drv] speed-loop deadzone FF = ");
                      uart_dbg_put_int(g_drv_dz); uart_dbg_puts("%\n"); }
                  return;
        case 'e': if (v >= 0 && v <= 300) g_pos_tol = v; break;     /* 位置到位死区 counts */
        case 'x': g_m1duty = clampi(v, -PWM_CAP, PWM_CAP); break;   /* DUAL(m5): M1 直驱 PWM% */
        case 'y': g_m2duty = clampi(v, -PWM_CAP, PWM_CAP); break;   /* DUAL(m5): M2 直驱 PWM% */
        case 'v': g_dv = clampi(v, -600, 600); break;   /* 差速(m6/m7): 线速度. m6=PWM% m7=RPM */
        case 'r': g_dw = clampi(v, -600, 600); break;   /* 差速(m6/m7): 角速度(左转为正). v0 r30=原地左转 */
        /* 陀螺零偏标定: 车必须完全静止 ~2s。标完自动 apply 并打印零偏。赛场每次上电都要做(零偏随温漂) */
        case 'k': if (g_imu_ok) { attitude_bias_start(&g_att); g_cal_left = CFG_IMU_CAL_N;
                      uart_dbg_puts("[imu] bias cal start, KEEP STILL ~2s\n"); }
                  else uart_dbg_puts("[imu] not ready\n");
                  break;
        case 'o': attitude_reset_yaw(&g_att, 0.0f); uart_dbg_puts("[imu] yaw=0\n"); break;   /* yaw 归零(转弯前置基准) */
        /* 定轴: a<0|1|2> 选竖直轴 / s<1|-1> 选偏航符号。零偏是在"置换+定符号之后"的坐标里
         * 测出来的 => 一改这两个值旧零偏就失效, 必须清掉并提示重标, 否则会拿错轴的零偏去积分。 */
        case 'a': if (v >= 0 && v <= 2) { g_yaw_axis = v; yaw_frame_changed("axis"); } break;
        case 's': g_yaw_sign = (v >= 0) ? 1 : -1; yaw_frame_changed("sign"); break;
        /* u0 = 编码器计数页(默认) / u1 = 水平仪页(定轴时边挪车边看) / u2 = RUN 页(走时+状态+里程+球位)。
         * 切页时重画静态层。u2 也是"跑完念数"的那一页 —— 进 m11 会自动切到它。 */
        case 'u': if (v >= 0 && v <= 2) { g_disp = v; g_lv_static = 0; g_disp_dirty = 1;
                      uart_dbg_puts(v == 2 ? "[lcd] page=RUN (走时/状态/里程/球位)\n"
                                  : v == 1 ? "[lcd] page=LEVEL (u0 回计数页; 挪车让小球进绿环)\n"
                                           : "[lcd] page=COUNT\n"); }
                  break;
#if CFG_LINE_UART_EN
        /* K: 虚拟按键 —— 等价于"人按了一下启动键"。按键没接线时靠它把整条任务链验完
         * (IDLE->RUN->计时->到达->BRAKE->DONE), 也可在 RUN 中再发一次当急停。
         * 60ms > 消抖窗 20ms ⇒ 一条命令 = 一次干净按压, 不会连触发。 */
        case 'K': g_btn_virt_until = g_st / ST_PER_MS + 60u;
                  uart_dbg_puts("[task] 虚拟按键 K (60ms)\n");
                  return;
        /* G<0|1|2>: 循迹**现场标定** —— 这是循迹能不能用的命门, 不是可选项。
         *   G0 = 车放**白底**上采一次(全部离线)   G1 = 车放**线上**采一次   G2 = 回读标定状态
         * 为什么必须现场标: 地面反射率/环境光/探头离地高度一变, 写死的阈值就瞎(line.h 文件头有账)。
         * ⚠ 顺序无所谓, 但两次都要采; 采完 G2 看 cal=YES 且 bad=-1 才算好。 */
        case 'G': {
            int dig[LF_CH]; int raw[LINE_MAX_CH]; int i, fresh;
            fresh = lf_get_digital(&g_lf, g_st / ST_PER_MS, CFG_LINE_MAX_AGE_MS, dig);
            if (v == 0 || v == 1) {
                if (!fresh) { uart_dbg_puts("[line] 拿不到新鲜帧, 标定拒绝 —— 先让模块发数据(见 L/rx)\n"); break; }
                /* ⚠ **喂的是合成常量, 不是当前读数** —— 理由同 line_init 处那段长注释:
                 * 数字量下白=0/黑=1000 是定义, 而"采当前读数"会让离线的 6 路 ref_b==ref_w
                 * ⇒ 对比度 0 ⇒ cal 永远 NO。所以 G0/G1 现在**车放哪儿都能过**,
                 * 它退化成"确认链路活着 + 把参考置成定义值"。开机已自动置好, 这两条是冗余保险。 */
                for (i = 0; i < LINE_MAX_CH; i++) raw[i] = (v == 1) ? 1000 : 0;
                if (v == 0) { line_cal_white(&g_line, raw); uart_dbg_puts("[line] cal WHITE 已置(合成: 全 0)\n"); }
                else        { line_cal_black(&g_line, raw); uart_dbg_puts("[line] cal BLACK 已置(合成: 全 1000)\n"); }
            }
            for (i = 0; i < LINE_MAX_CH; i++) raw[i] = (i < LF_CH && fresh && dig[i] == 0) ? 1000 : 0;  /* 0=在黑线上 */
            uart_dbg_puts("[line] cal="); uart_dbg_puts(line_calibrated(&g_line) ? "YES" : "NO");
            uart_dbg_puts(" bad_ch=");    uart_dbg_put_int(line_bad_channel(&g_line));
            uart_dbg_puts(" fresh=");     uart_dbg_put_int(fresh);
            uart_dbg_puts(" dig:");
            for (i = 0; i < LF_CH; i++) { uart_dbg_putc(' '); uart_dbg_put_int(dig[i]); }
            uart_dbg_puts("\n");
            return; }
#endif
#if CFG_ESP_UART_EN
        /* b<秒>: 进 AT 桥接(本口 <-> 车载 ESP 原样对接), 到期自动退出。范围 5~300s, 缺省 30。
         * 用途: 车载 ESP 只连 MCU、PC 碰不到它 ⇒ 靠这条给它发 AT 配置。详见 bridge_pump() 注释。 */
        case 'b': {
            int bs = (v >= 5 && v <= 300) ? v : 30;
            stop_all();                     /* 桥接期间不解析命令 => 先停干净, 别留着能动的机器 */
            uart_dbg_puts("[uart] bridge ON ");
            uart_dbg_put_int(bs);
            uart_dbg_puts("s : this port <-> ESP(UART3) RAW; telemetry muted; no cmd parsing; auto-exit\n");
            g_bridge_end = g_st + (uint32_t)bs * 1000u * ST_PER_MS;
            if (g_bridge_end == 0) g_bridge_end = 1;   /* 防 wrap 到 0 被当成"未桥接" */
            return;                         /* 不打 print_status: 那会污染 AT 通道 */
        }
        /* l<mask>: 遥测往哪些口打印。**0=全关** / 1=有线(DAP VCOM) / 2=无线(ESP) / 3=双发(默认)。
         * 什么时候要改: ① 整定用 f20~f25 时双发会吃掉主循环一半时间(每字节发两遍) -> 只留一个口;
         *   ② 🔴 **正式测试必须 `l0`** —— 官方答疑 Q62 明文"测试期间仅允许图传工作"。
         * 🔁 **2026-07-31: `l0` 现在生效**。原先 `if (v > 0)` 这道门把它挡掉了(uart_dbg.c 里还有第二道,
         *   已一并放开) ⇒ 那时**根本没有一键关无线的手段**, 是合规缺口而非保护。
         * ⚠ **回执必须在关闭之前打** —— 关完再打就没有任何口收得到, 现场会误判成"板子死了"。
         * 关掉后怎么看车: **LCD RUN 页 `u2`**(走时/状态/里程/球位) 是独立通道; 命令通道(RX)与 sink 无关,
         *   随时 `l3` 恢复。 */
        case 'l': if (v >= 0 && v <= 3) {
                      if (v == 0) {   /* 先打回执, 再静音 —— 顺序不能反 */
                          uart_dbg_puts("[uart] sinks->0 : ALL telemetry OFF (Q62 test mode).\n"
                                        "[uart]   watch the car on LCD RUN page: send u2 . send l3 to restore.\n");
                      }
                      uart_dbg_set_sinks((uint32_t)v);
                      if (v != 0) {
                          uart_dbg_puts("[uart] sinks="); uart_dbg_put_int((int)uart_dbg_get_sinks());
                          uart_dbg_puts(" (0=off, 1=wired DAP, 2=wireless ESP, 3=both)\n");
                      }
                  } else {
                      uart_dbg_puts("[uart] l<mask> 范围 0..3 (0=全关 1=有线 2=无线 3=双发)\n");
                  }
                  break;
#endif
        case 'g': imu_dump(); return;   /* IMU 验活读数(陀螺到货后 bring-up 用) */
        case '?': break;
        default:  break;
    }
    if (g_mode != mode_before) g_mode_at = g_st;   /* 换了模式 -> 硬上限重新起算 */
    print_status();
#if CFG_LINE_UART_EN
    if (s[0] == '?') print_task();   /* `?` 顺带回读任务层 —— 现场问"它到底在哪一步"就靠这行 */
#endif
    /* `?` 顺带回读滚球层。只在进过 m12 之后才有意义, 但无条件打——现场最怕的是"以为在控制、
     * 其实 BLOCKED 在 NO_MEAS"，那一行必须一眼看到。 */
    if (s[0] == '?') print_ball();
}
/* ==== 命令格式门(只在编了无线口时才需要) ====
 * 只接受 `<字母>[-][数字...]` 这一种形状, 可选 `#` 前缀。多一个空格、多一个字母 => 拒。
 *
 * 为什么必须有(不是防御性编程, 是修一个已坐实的 bug):
 *   ESP-01S 每次上电都从 TXD 吐一段 boot 日志, 里面 `tail 0` / `tail 4` 三行的**首字符是 `t`**
 *   = 命令"设定目标值" => 每次 ESP 上电必静默改目标 + 清 PID 积分; 日志里还有纯乱码行,
 *   撞上 `x`/`y` 就是直接驱动电机。而查电机/编码器/PID 永远查不到凶手在 ESP 的启动日志里。
 *   证据: esp_boot_risk.ps1 拿 650 字节真实 boot 流在 PC 上回放, 22 行会被当命令、其中 3 行触发 `t`。
 * 兼容性: 现有全部脚本发的是 m3/t900/p150/z/? 这种形状 => 100% 过门(PC 侧已用模拟器实测 8/8 ACCEPT)。 */
#if CFG_ESP_UART_EN
/* 门本体在 cmd_gate.h (static inline, 不依赖 HAL) —— 与 pc_test/test_cmd_gate.c 共用同一份代码,
 * 免得像 esp_fake_mcu.ps1 那样出现"自称 faithful copy 而原件不存在"的两份实现。 */

/* 一个来源的字节流 -> 行 -> 命令。buf/len 由调用方按来源分开持有(见 cbuf/ebuf 注释)。
 * gate=1 时过格式门(无线口必须过; 有线口也过, 反正现有脚本 100% 兼容)。 */
static void feed_cmd_stream(uint8_t ch, char *buf, int *len, int gate)
{
    if (ch == '\r' || ch == '\n') {
        int n = *len;
        *len = 0;
        if (n <= 0) return;
        /* 上电静默窗: 字节照读走(不读走的话解禁后照样会被解析), 但不解析成命令 */
        if (g_st < (uint32_t)CMD_MUTE_MS * ST_PER_MS) { g_cmd_rej++; return; }
        if (gate && !cmd_format_ok(buf, n))            { g_cmd_rej++; return; }
        run_cmd(buf, n);
    } else if (*len < 15) {
        buf[(*len)++] = (char)ch;
    }
}
#endif

/* ==== 视觉帧分流 ====
 * 以 '$' 开头的整行**不进命令通道**, 直接喂给帧解析器(uart_frame.c)。
 *
 * 为什么这么做(而不是给相机单开一路 UART):
 *   ① 现在就能测整条视觉伺服链 —— **一台相机都不需要**, PC 直接往调试口发
 *      `$V,1,200,240,900*HH` 就能让车按"看到目标"去动(m10)。相机没到手也能把控制半段验完。
 *   ② 相机到手后接哪个口都行(有线调试口的 USB-TTL 或无线口), 不必先改 syscfg 加外设。
 *   ③ 命令格式门本来就会把 `$...` 拒掉并计进 rej ⇒ 不分流的话视觉帧会变成一堆"被拒的噪声"。
 * 返回 1 = 本字节已被视觉通道吃掉。
 * ⚠ 已知边界: 若一帧被截断且再也没有 '\n', 解析器会一直处于 in_frame 而把后续字节(包括真命令)
 *   吞掉 —— 但最多吞 UF_BUF_LEN(48) 个字节, 之后 overflow 会强制退出帧态。所以最坏情况是
 *   "丢一条命令", 不会锁死命令通道。 */
static int vision_grab(uint8_t ch)
{
    if (ch == '$' || g_uf.in_frame) {
        uf_push(&g_uf, (char)ch, g_st / ST_PER_MS);
        return 1;
    }
    return 0;
}

/* 注入通道(调试口 / ESP)的 '$' 分流 —— 走**独立**解析器, 完整理由见 g_uf_inj 声明处。
 * 关键点: 判据只看 `g_uf_inj.in_frame`, **绝不看 g_uf.in_frame** —— 否则相机灌帧时又会吞命令。
 * 返回 1 = 本字节已被视觉通道吃掉(调用方不要再送进命令流)。 */
static int vision_grab_inj(uint8_t ch)
{
    if (ch == '$' || g_uf_inj.in_frame) {
        if (uf_push(&g_uf_inj, (char)ch, g_st / ST_PER_MS)) {
            /* 刚解析成功一帧 ⇒ 并入主实例, 让下游对"帧来自哪一路"无感。
             * 只搬 last/have_frame, **不动计数器** —— 计数在 print_vision 里按两个实例求和,
             * 那样既不会重复计数, 也不必维护"上次同步到哪"的增量状态。 */
            g_uf.last       = g_uf_inj.last;
            g_uf.have_frame = 1;
        }
        return 1;
    }
    return 0;
}

/* ==== VIS_UART(PB16) 物理层计数器 ====
 * 为什么需要它: uf_* 那套统计只在**字节已经进到解析器**之后才动, 所以 "ok=0 且全 0" 这一种读数
 * 同时对应三个完全不同的世界 —— 线没接上 / 接上了但电平·波特率错(收到的是垃圾) / 字节进来了但
 * 被 vision_grab 当非帧字节丢掉。真机上这三者的修法毫不相干(动线 / 换波特率 / 改代码), 而
 * 2026-07-31 那次 bring-up 恰好卡在这个岔口: MCU 侧注入全 PASS, 真相机零帧, 无从判断该动哪边。
 * 加两个物理层计数器就能一次分开:
 *   bytes=0 err=0  -> RX 脚上**一个边沿都没有**: K230 没跑 / TX 接到 PB15 了 / 没共地
 *   bytes=0 err>0  -> 有信号但**帧结构对不上**: 波特率不符 / 电平不对 / 接的是别的信号
 *   bytes>0        -> 物理层通了, 问题在协议层(看 bad_csum / bad_form / overflow)
 * tail 存最近 8 个原始字节: 波特率错时它是稳定的乱码, 一眼可辨(而 "$BP,1,+" 就说明全好)。
 * 只在 poll_uart 里写、只在 print_vision 里读, 单线程, 不需要临界区。 */
/* ==== 视觉字节的接收: 在 5kHz SysTick 里抽 FIFO, 解析仍留主循环 ====
 *
 * 🔴 为什么非这么做不可(2026-07-31 真机实测):
 *   一帧 `$BP,1,+9.62*1B\r\n` 是 17 字节, 在 115200 下**背靠背 1.5ms** 打进来, 而 MSPM0 的
 *   RX FIFO 只有 4 深(生成码里阈值还是 1_2_FULL=2) ⇒ 想不丢字节, 必须**每 ~350us 抽一次**。
 *   主循环要刷 GC9A01(SPI 全屏可达几十 ms)又要打遥测, 根本给不出这个节拍。
 *   实测后果: PB5 上 bytes=28907 而 **ok=0** —— 一帧都没解析成功, bad_csum=355 / bad_form=590,
 *   错误掩码恒为 **0x10=OVERRUN**(不是 framing ⇒ 波特率和电平都没问题, 纯粹是抽得不够快)。
 *
 * ⛔ 为什么不开 UART 的 RX 中断(那是最"标准"的做法):
 *   car.syscfg 里有一条 2026-07-29 立的禁令 —— 在 UART1 上开过 RX 中断, 结果 5V 电平持续制造
 *   帧/噪声错误中断, 把主循环吃干、固件彻底哑掉, 两种 ISR 写法都锁死。那条禁令还在。
 *   而 5kHz SysTick(200us) 已经**小于 350us 要求**且是跑了几周的稳定 ISR ⇒ 白拿一个够快的节拍,
 *   不新增中断源、不动 NVIC、不碰那条禁令。这是"用已有的东西满足时序"而不是"加一个新风险"。
 *
 * ISR 里只搬字节、不解析: uf_push 会写 g_uf(主循环要读 g_uf.last), 放进 ISR 就有撕裂风险。
 * 环形缓冲单生产者(SysTick)单消费者(主循环), head/tail 各只被一方写 ⇒ M0+ 上 16 位对齐读写
 * 本身原子, 不需要临界区。 */
/* 抽一路 UART 的 FIFO 进环形缓冲 + 记错误标志。**只在 SysTick ISR 里调用**。
 * 用 raw 中断状态读错误 ⇒ 不需要使能任何中断, 也不会跟别处的中断配置打架。 */
static void vis_drain_isr(UART_Regs *uart, vis_rx_stat_t *s)
{
    uint8_t ch;
    uint32_t es = DL_UART_getRawInterruptStatus(uart,
                      DL_UART_INTERRUPT_FRAMING_ERROR | DL_UART_INTERRUPT_PARITY_ERROR |
                      DL_UART_INTERRUPT_BREAK_ERROR   | DL_UART_INTERRUPT_OVERRUN_ERROR);
    if (es) {
        s->err++;
        s->err_last = es;
        DL_UART_clearInterruptStatus(uart, es);
    }
    while (DL_UART_receiveDataCheck(uart, &ch)) {
        uint16_t h = g_vr_head;
        uint16_t n = (uint16_t)((h + 1u) & (VIS_RING_SZ - 1u));
        s->bytes++;
        s->tail[s->tail_n & 7u] = ch;
        s->tail_n = (uint8_t)((s->tail_n + 1u) & 7u);
        if (n != g_vr_tail) { g_vring[h] = ch; g_vr_head = n; }
        else                { g_vr_drop++; }     /* 主循环卡了 >600ms; 丢弃比覆盖旧数据好 */
    }
}

#if CFG_LINE_UART_EN
/* 等 ms 毫秒, **期间持续轮询** UART1 —— 不这么写的话对方的回应会烂在 FIFO 里(FIFO 只有几字节深,
 * 溢出就永远看不到了), 于是"其实回了但我们没接"会被误判成"没回"。 */
static void line_wait_ms(uint32_t ms)
{
    uint32_t t0 = g_st;
    while ((uint32_t)(g_st - t0) < ms * ST_PER_MS)
        linesens_poll(g_st / ST_PER_MS);
}
static void line_selftest(void)
{
    int n = linesens_selftest_loopback();
    uart_dbg_puts("\n[line] loopback selftest: sent 5 got "); uart_dbg_put_int(n);
    if (n == 5) {
        uart_dbg_puts("  => PASS: UART1外设+波特率+轮询/缓冲**整条接收链路都好**。\n"
                      "[line]    ⇒ 之前的 rx=0 不是固件问题: 要么模块不主动发(试 Q/X), 要么 PB5 外部走线/焊点/电平有问题。\n");
    } else {
        uart_dbg_puts("  => FAIL: 连内部回环都收不到 ⇒ **别再查接线了**, 是固件/外设配置问题。\n");
    }
    linesens_dump();
}
/* 扫查询字节: 每个值发一次, 看有没有回应。命中就打出来。 */
static void line_sweep_query(void)
{
    int hits = 0;
    uart_dbg_puts("\n[line] sweep 0..255 @baud="); uart_dbg_put_int((int)linesens_get_baud());
    uart_dbg_puts(" (每个值等 25ms)\n");
    for (int b = 0; b <= 255; b++) {
        linesens_clear();
        linesens_tx((uint8_t)b);
        line_wait_ms(25);
        uint32_t got = linesens_rx_total();
        if (got > 0) {
            hits++;
            uart_dbg_puts("[line]  HIT tx="); uart_dbg_put_int(b);
            uart_dbg_puts(" -> rx=");         uart_dbg_put_int((int)got);
            uart_dbg_puts("\n");
        }
    }
    uart_dbg_puts("[line] sweep done, hits="); uart_dbg_put_int(hits);
    if (hits == 0)
        uart_dbg_puts(" => 256 个值全无回应: 该波特率下模块不应答。换 B<baud> 再扫, 或它本就该主动发(则查走线/电平)。\n");
    else
        uart_dbg_puts(" => 命中的值就是查询帧候选; 用 X<值> 复现一次再看 L 的字节。\n");
}
#endif
static void poll_uart(void)
{
    uint8_t ch;
    /* 循迹模块的字节走独立缓冲, **绝不进命令流** —— 传感器数据里出现 'z' 之类会误触发急停。
     * 时基用与视觉帧同一个 g_st/ST_PER_MS, 好让帧间隙判定与遥测时间戳对得上。 */
    linesens_poll(g_st / ST_PER_MS);
    /* 相机专用口(UART2/PB15-16): 字节**只喂帧解析器, 不进命令通道**。
     * 为什么不复用 vision_grab 的"非 $ 就转命令"那条路: 相机永远不该发命令, 而画面噪声/半截帧里
     * 出现字母是常态 —— 让它进命令流就等于给了它触发 t/z/m 的机会。丢弃非帧字节是正确行为。
     * 为什么相机要独占一个口: DBG_UART 的 RX 被 DAP 的 TX 占着、ESP_UART 的 RX 被 ESP#2 占着,
     * 两个都接不了第三个发送端(见 car.syscfg 里 VIS_UART 那段的完整理由)。 */
    /* 相机字节由 SysTick 抽进环形缓冲(见 vis_drain_isr 的账), 这里只负责把它喂给帧解析器。
     * 一次最多吃满整环 ⇒ 不会因为"边喂边来"在这里转不出去。 */
    {
        uint16_t t = g_vr_tail;
        uint32_t guard = VIS_RING_SZ;
        while (t != g_vr_head && guard--) {
            (void)vision_grab(g_vring[t]);
            t = (uint16_t)((t + 1u) & (VIS_RING_SZ - 1u));
            g_vr_tail = t;
        }
    }
    /* ⚠ 这两路必须用 vision_grab_inj(独立解析器), **不能**用 vision_grab —— 后者看的是相机的
     *   g_uf.in_frame, 相机灌帧时会把这里的命令字符吞掉(见 g_uf_inj 声明处那笔账)。 */
#if CFG_ESP_UART_EN
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch))
        if (!vision_grab_inj(ch)) feed_cmd_stream(ch, cbuf, &clen, 1);
    while (DL_UART_receiveDataCheck(ESP_UART_INST, &ch))
        if (!vision_grab_inj(ch)) feed_cmd_stream(ch, ebuf, &elen, 1);
#else
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch)) {
        if (vision_grab_inj(ch)) continue;
        if (ch == '\r' || ch == '\n') { if (clen > 0) { run_cmd(cbuf, clen); clen = 0; } }
        else if (clen < 15) cbuf[clen++] = (char)ch;
    }
#endif
}

#if CFG_ESP_UART_EN
/* ==== AT 桥接模式(命令 `b<秒>`) ====
 * 把有线调试口(UART0/DAP VCOM) 与 车载 ESP(UART3) **原样字节对接**, 让 PC 能直接跟车上那块
 * ESP 对话(发 AT、读回复), 中间不解析、不过格式门、不打遥测。
 *
 * 为什么必须有它: 车载 ESP 的串口只连到 MCU 的 PB2/PB3, 板上没有第二个串口座 ⇒
 *   一旦它装上车, PC 就再也碰不到它, 而"没配过的模块永远不会转发数据"(见 cmd_gate.h 同族问题)。
 *   没有桥接就只能拆板子; 有了它, 换模块/改 SSID/改信道(赛场 2.4G 拥挤时要换 ch1/ch6)全都不用拆车。
 *
 * 退出方式 = **超时自动退出**, 不用逃逸序列。理由: 桥接期间每个字节都要原样转发, 任何"魔法序列"
 *   都会和 ESP 自己的 `+++` 抢语义; 而超时零歧义、脚本也好写(发 `b120` 然后 120s 内随便聊)。
 * 安全: 进入前先 stop_all()(桥接期间不解析命令 ⇒ 没法急停, 所以先把机器停干净)。
 */
static void bridge_pump(void)
{
    uint8_t ch;
    /* 直接用 DL 调用, 绕开 uart_dbg 的 sink 分发 —— 桥接要的是"一对一原样搬", 不是广播 */
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch))
        DL_UART_transmitDataBlocking(ESP_UART_INST, ch);
    while (DL_UART_receiveDataCheck(ESP_UART_INST, &ch))
        DL_UART_transmitDataBlocking(DBG_UART_INST, ch);
}
#endif

static void i32_to_field(char *b, int32_t v, int w);   /* 前置声明: 定义在本文件后半 */

/* ==== 水平仪页(LCD 定轴助手) ====
 * 圆屏上画一个"重锤/滚珠"式水平仪: 同心容差环 + 一个小球。**小球滚向下沉的那一侧**
 * (即车哪边低, 球往哪边跑) => 操作直觉 = "把球所在那一侧抬起来"。
 *
 * 为什么需要它: 定轴(L1)要求车放平, 但人挪车时没有任何反馈, 只能反复跑脚本猜。
 * 阈值与"为什么越平越好"的量化理由全在 config.h §6.5。
 *
 * 成本控制(这块屏 2 字节/像素、阻塞式 SPI, 见 gc9a01.c DrawChar 注释):
 *   静态部分(标题/三个容差环)只画一次; 每帧只 ① 擦掉小球旧位置 ② 画新位置 ③ 补画被擦到的环点;
 *   文字仅在内容变化时重绘(固定宽度、不透明底 => 覆盖式, 不清屏不闪)。
 *   本页只在 IDLE 用(进任何运动模式会自动切回计数页), 所以它的开销不会影响控制环。
 */
#define LV_CX   120
#define LV_CY   120
#define LV_BR   7                        /* 小球半径 */
#define LV_R_OK   ((int16_t)(CFG_LEVEL_OK_DEG   * CFG_LEVEL_PX_PER_DEG))
#define LV_R_GOOD ((int16_t)(CFG_LEVEL_GOOD_DEG * CFG_LEVEL_PX_PER_DEG))
#define LV_R_PASS ((int16_t)(CFG_LEVEL_PASS_DEG * CFG_LEVEL_PX_PER_DEG))

static void lv_draw_rings(void)
{
    GC9A01_DrawCircle(LV_CX, LV_CY, LV_R_PASS, LCD_ORANGE, 3);   /* 14° 底线 */
    GC9A01_DrawCircle(LV_CX, LV_CY, LV_R_GOOD, LCD_YELLOW, 2);   /* 5°  良 */
    GC9A01_DrawCircle(LV_CX, LV_CY, LV_R_OK,   LCD_GREEN,  1);   /* 2°  优(实线) */
    GC9A01_DrawPixel(LV_CX, LV_CY, LCD_WHITE);                   /* 圆心 */
}

static void lv_line(int idx, int16_t y, const char *s, uint16_t fg, uint8_t scale)
{
    if (idx >= 0 && idx < 2) {
        int same = 1;
        for (int i = 0; i < 19; i++) { if (g_lv_txt[idx][i] != s[i]) { same = 0; break; } if (!s[i]) break; }
        if (same) return;                                        /* 内容没变 -> 不重绘(省 SPI, 不闪) */
        for (int i = 0; i < 19; i++) { g_lv_txt[idx][i] = s[i]; if (!s[i]) break; }
        g_lv_txt[idx][19] = 0;
    }
    GC9A01_DrawStringCentered(y, s, fg, LCD_BLACK, scale);
}

/* 定宽写入: 把 v 以 "整数.小数1位" 写进 b(共 w 字符, 右对齐), 供覆盖式刷新 */
static void fix1(char *b, float v, int w)
{
    int t = (int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
    int neg = t < 0; if (neg) t = -t;
    char tmp[12]; int n = 0;
    tmp[n++] = (char)('0' + (t % 10)); tmp[n++] = '.'; t /= 10;
    if (t == 0) tmp[n++] = '0';
    while (t) { tmp[n++] = (char)('0' + (t % 10)); t /= 10; }
    if (neg) tmp[n++] = '-';
    int i = 0; for (int pad = w - n; pad > 0; pad--) b[i++] = ' ';
    while (n > 0) b[i++] = tmp[--n];
    b[i] = 0;
}

/* accel_g[3] = 原始传感器系加速度(g)。整页刷新一次。 */
static void disp_level(const float a[3])
{
    if (!g_lv_static) {
        GC9A01_FillScreen(LCD_BLACK);
        GC9A01_DrawStringCentered(20, "LEVEL", LCD_GRAY, LCD_BLACK, 1);
        lv_draw_rings();
        g_lv_static = 1;
        g_lv_px = g_lv_py = -999;
        g_lv_txt[0][0] = g_lv_txt[1][0] = 1;   /* 置成不可能值 -> 强制首帧写字 */
    }

    /* 1) 模长 + 竖直轴(取 |a| 最大的那一轴) */
    float mag = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    int mg = (int)(mag * 1000.0f + 0.5f);
    int d = 0;
    for (int i = 1; i < 3; i++) if (fabsf(a[i]) > fabsf(a[d])) d = i;

    /* 2) 用 attitude_axis_map(已 PC 单测)把竖直轴搬到 slot2 => slot0/1 天然就是两个水平分量。
     *    复用它而不是另写一套 if-else: 同一套置换, 与航向层的轴向约定不会漂。 */
    float m[3];
    attitude_axis_map(d, a, m);
    float h = sqrtf(m[0]*m[0] + m[1]*m[1]);              /* 水平分量模长 */
    float tilt = atan2f(h, fabsf(m[2])) * 57.295779513f; /* 与竖直方向的夹角(度) */

    /* 3) 小球位置: 方向来自水平分量, 半径来自倾角(线性 px/度)。LCD y 向下 =>
     *    车哪边低, 重力在那边分量为正, 球就往那边跑(重锤直觉)。 */
    int16_t px = 0, py = 0;
    if (h > 1e-4f) {
        float k = tilt * (float)CFG_LEVEL_PX_PER_DEG / h;
        px = (int16_t)(m[0] * k);
        py = (int16_t)(m[1] * k);
    }
    const int16_t lim = 100;                              /* 别画出圆屏 */
    if (px >  lim) px =  lim;
    if (px < -lim) px = -lim;
    if (py >  lim) py =  lim;
    if (py < -lim) py = -lim;

    /* 4) 判定 */
    uint16_t col; const char *verd;
    int a_bad = (mg < CFG_LEVEL_A_MIN_MG || mg > CFG_LEVEL_A_MAX_MG);
    if      (a_bad)                        { col = LCD_MAGENTA; verd = "HOLD STILL"; }
    else if (tilt <= CFG_LEVEL_OK_DEG)     { col = LCD_GREEN;   verd = "OK  BEST  "; }
    else if (tilt <= CFG_LEVEL_GOOD_DEG)   { col = LCD_YELLOW;  verd = "OK  GOOD  "; }
    else if (tilt <= CFG_LEVEL_PASS_DEG)   { col = LCD_ORANGE;  verd = "PASS(min) "; }
    else                                   { col = LCD_RED;     verd = "TILT      "; }

    /* 5) 只重画小球: 擦旧 -> 补被擦掉的环点 -> 画新 */
    if (px != g_lv_px || py != g_lv_py) {
        if (g_lv_px != -999) {
            GC9A01_FillCircle(LV_CX + g_lv_px, LV_CY + g_lv_py, LV_BR, LCD_BLACK);
            lv_draw_rings();                              /* 环可能被擦到, 补一次(虚线环很便宜) */
        }
        GC9A01_FillCircle(LV_CX + px, LV_CY + py, LV_BR, col);
        g_lv_px = px; g_lv_py = py;
    }

    /* 6) 两行文字(仅变化才重绘) */
    char l0[20], l1[20], nb[8];
    /* 上行: 竖直轴是谁 + 模长, 例 "V=Z+ |a|= 999mg" */
    l0[0]='V'; l0[1]='='; l0[2]=(char)('X'+d); l0[3]=(a[d]>=0?'+':'-');
    l0[4]=' '; l0[5]='|'; l0[6]='a'; l0[7]='|'; l0[8]='=';
    i32_to_field(nb, mg, 4);
    l0[9]=nb[0]; l0[10]=nb[1]; l0[11]=nb[2]; l0[12]=nb[3];
    l0[13]='m'; l0[14]='g'; l0[15]=0;
    lv_line(0, 36, l0, a_bad ? LCD_MAGENTA : LCD_GRAY, 1);
    /* 下行: 判定 + 倾角, 例 "OK  BEST   1.2d" */
    { int i = 0; while (verd[i] && i < 10) { l1[i] = verd[i]; i++; }
      fix1(nb, tilt, 5);
      l1[i++]=nb[0]; l1[i++]=nb[1]; l1[i++]=nb[2]; l1[i++]=nb[3]; l1[i++]=nb[4];
      l1[i++]='d'; l1[i]=0; }
    lv_line(1, 186, l1, col, 2);
}

/* int32 -> 右对齐固定宽度字符串(左补空格), 供 LCD 覆盖式刷新:
 * 固定宽度 => 位数增减时旧字符被空格/新字符原位覆盖, 无需清屏、无残影、不左右漂移。 */
static void i32_to_field(char *b, int32_t v, int w)
{
    char tmp[12]; int t = 0, i = 0, pad; uint32_t x;
    int neg = (v < 0);
    x = neg ? (uint32_t)(-v) : (uint32_t)v;
    if (x == 0) tmp[t++] = '0';
    while (x) { tmp[t++] = (char)('0' + (x % 10)); x /= 10; }
    if (neg) tmp[t++] = '-';
    for (pad = w - t; pad > 0; pad--) b[i++] = ' ';   /* 左补空格到固定宽度 */
    while (t > 0) b[i++] = tmp[--t];
    b[i] = 0;
}

#if ENC_PROBE
/* 读一个引脚原始电平 -> 0/1 */
static int rd(GPIO_Regs *port, uint32_t pin) { return DL_GPIO_readPins(port, pin) ? 1 : 0; }

/* 把 "前缀=" + 固定宽计数 拼进 buf, 返回写入长度(不含末尾null由调用者收尾) */
static int put_cnt(char *b, char c0, char c1, int32_t v, int w)
{
    b[0] = c0; b[1] = c1; b[2] = '=';
    i32_to_field(b + 3, v, w);   /* 写 w 个字符 + null */
    return 3 + w;
}

/*
 * 编码器硬件/软件分层探针。永不返回, 全程不驱动电机(安全)。
 * 屏上(从上到下):
 *   ENC PROBE v1        <- 版本条(证明跑的是新固件)
 *   <build time>        <- 编译时刻(每次重编必变, 双保险证明新固件)
 *   A1=x A2=x           <- 两路 A 相(中断触发脚 PA7/PB20)原始电平, 手慢转看它 0/1 跳变
 *   B1=x B2=x           <- 两路 B 相(判向脚 PB19/PB21)原始电平
 *   E1=n E2=n           <- 软件轮询数到的 A 相上升沿(不经中断)
 *   I1=n I2=n           <- 中断(ISR)数到的计数(encoder.c 的 g_cnt)
 * 判读: 慢转某轮 -> 对应 A/B 电平在 0/1 间跳 = 信号已进 MCU 脚;
 *   E 只涨 I 不涨 = 中断/软件层问题; E 与 I 都涨 = 该路其实在工作;
 *   电平纹丝不动 & E/I 都 0 = 信号没到脚(硬件), 万用表逐段量。
 *   四脚里"哪几个死"直接区分: A1/A2 都死+B 都死 => 共用供电/地; 只一路死 => 那路接线/器件。
 */
static void enc_probe_run(void)
{
    GC9A01_Backlight(1);
    GC9A01_Init();
    motor_init();
    motor_stop_all();          /* 电机滑行停, 探针全程不给 PWM */
    encoder_init();            /* 使能中断, 以便对比 ISR 计数 */

    GC9A01_FillScreen(LCD_BLACK);
    GC9A01_DrawStringCentered(14, "ENC PROBE v2", LCD_GREEN, LCD_BLACK, 2);
    GC9A01_DrawStringCentered(40, __TIME__, LCD_GRAY, LCD_BLACK, 1);   /* 编译时刻: 每次重编必不同 */
    GC9A01_DrawStringCentered(196, "AUTO-SPIN TEST", LCD_GRAY, LCD_BLACK, 1);

    uart_dbg_puts("\n[probe] ENC PROBE v2 auto-spin  build "); uart_dbg_puts(__DATE__);
    uart_dbg_puts(" "); uart_dbg_puts(__TIME__); uart_dbg_puts("\n");
    uart_dbg_puts("[probe] auto-spin M1 3s -> stop -> M2 3s (40% PWM), stops after ~40s\n");
    uart_dbg_puts("[probe] cols: DRV(driven motor) L(raw level) E(poll edge,no IRQ) I(ISR count)\n");

    /* 软件轮询边沿计数(不经中断) */
    uint32_t e_cnt[2] = { 0, 0 };
    int prevA[2] = { -1, -1 };
    /* LCD 变化检测缓存 */
    int    last_lv[4]   = { -1, -1, -1, -1 };
    int32_t last_e[2]   = { -1, -1 };
    int32_t last_i[2]   = { -1, -1 };
    uint32_t t = 0;

    while (1) {
        encoder_poll();   /* 定时采样正交解码(替代边沿中断): 每 tick 采一次 A/B 更新计数 */

        int a1 = rd(GPIOA, GPIO_ENC_ENC1_A_PIN);   /* PA7  */
        int b1 = rd(GPIOB, GPIO_ENC_ENC1_B_PIN);   /* PB19 */
        int a2 = rd(GPIOB, GPIO_ENC_ENC2_A_PIN);   /* PB20 */
        int b2 = rd(GPIOB, GPIO_ENC_ENC2_B_PIN);   /* PB21 */

        /* 软件轮询: A 相 0->1 视为一次边沿(与中断完全独立的第二条通路) */
        if (prevA[0] == 0 && a1 == 1) e_cnt[0]++;
        if (prevA[1] == 0 && a2 == 1) e_cnt[1]++;
        prevA[0] = a1; prevA[1] = a2;

        /* --- 自动轻转: 电机替代手转(M1 转 3s -> 停 2s -> M2 转 3s -> 停 4s, 12s 循环).
         * 40% PWM 保守(7.4V 电机/12V 母线); t>40s 后永久停, 防长转/防跑车. --- */
        int drv = 0;                       /* 0=停 1=转M1 2=转M2 */
        if (t < 40000) {
            uint32_t cyc = t % 12000;
            if      (cyc >= 2000 && cyc < 5000)  drv = 1;
            else if (cyc >= 7000 && cyc < 10000) drv = 2;
        }
        motor_set(MOTOR_M1, drv == 1 ? 40 : 0);
        motor_set(MOTOR_M2, drv == 2 ? 40 : 0);

        int32_t i1 = encoder_count(ENC_1);
        int32_t i2 = encoder_count(ENC_2);

        /* --- LCD 刷新(仅变化才重绘, 固定宽度覆盖式, 不闪) --- */
        if (t % 20 == 0) {
            char buf[32];
            if (a1 != last_lv[0] || a2 != last_lv[1]) {
                buf[0]='A';buf[1]='1';buf[2]='=';buf[3]=(char)('0'+a1);
                buf[4]=' ';buf[5]=' ';
                buf[6]='A';buf[7]='2';buf[8]='=';buf[9]=(char)('0'+a2);buf[10]=0;
                GC9A01_DrawStringCentered(72, buf, LCD_WHITE, LCD_BLACK, 2);
                last_lv[0]=a1; last_lv[1]=a2;
            }
            if (b1 != last_lv[2] || b2 != last_lv[3]) {
                buf[0]='B';buf[1]='1';buf[2]='=';buf[3]=(char)('0'+b1);
                buf[4]=' ';buf[5]=' ';
                buf[6]='B';buf[7]='2';buf[8]='=';buf[9]=(char)('0'+b2);buf[10]=0;
                GC9A01_DrawStringCentered(100, buf, LCD_CYAN, LCD_BLACK, 2);
                last_lv[2]=b1; last_lv[3]=b2;
            }
            if ((int32_t)e_cnt[0] != last_e[0] || (int32_t)e_cnt[1] != last_e[1]) {
                int p = put_cnt(buf, 'E', '1', (int32_t)e_cnt[0], 6);
                buf[p++]=' ';
                p += put_cnt(buf + p, 'E', '2', (int32_t)e_cnt[1], 6);
                GC9A01_DrawStringCentered(136, buf, LCD_YELLOW, LCD_BLACK, 1);
                last_e[0]=(int32_t)e_cnt[0]; last_e[1]=(int32_t)e_cnt[1];
            }
            if (i1 != last_i[0] || i2 != last_i[1]) {
                int p = put_cnt(buf, 'I', '1', i1, 8);
                buf[p++]=' ';
                p += put_cnt(buf + p, 'I', '2', i2, 8);
                GC9A01_DrawStringCentered(160, buf, LCD_ORANGE, LCD_BLACK, 1);
                last_i[0]=i1; last_i[1]=i2;
            }
        }

        /* --- UART 第二通道(~200ms) + 电机电流(证明电机真的在通电/转) --- */
        if (t % 200 == 0) {
            uint16_t r0 = 0, r1 = 0;
            motor_read_current_raw(&r0, &r1);
            uart_dbg_puts("[probe] DRV="); uart_dbg_puts(drv==1?"M1":(drv==2?"M2":"--"));
            uart_dbg_puts(" L A1="); uart_dbg_put_int(a1);
            uart_dbg_puts(" B1=");          uart_dbg_put_int(b1);
            uart_dbg_puts(" A2=");          uart_dbg_put_int(a2);
            uart_dbg_puts(" B2=");          uart_dbg_put_int(b2);
            uart_dbg_puts(" | E ");         uart_dbg_put_int((int32_t)e_cnt[0]);
            uart_dbg_putc(',');             uart_dbg_put_int((int32_t)e_cnt[1]);
            uart_dbg_puts(" | I ");         uart_dbg_put_int(i1);
            uart_dbg_putc(',');             uart_dbg_put_int(i2);
            uart_dbg_puts(" | mA ");        uart_dbg_put_int((int)motor_current_ma(r0));
            uart_dbg_putc(',');             uart_dbg_put_int((int)motor_current_ma(r1));
            uart_dbg_puts("\n");
        }

        t++;
        delay_ms(1);   /* ~1kHz 轮询, 足够抓手转边沿 */
    }
}
#endif /* ENC_PROBE */

/* 编码器采样由 SysTick 5kHz 定时中断驱动(不放主循环)——主循环的阻塞式 UART 遥测每次会
 * stall 几 ms, 若在主循环 poll 会漏采/混叠 → 速度乱跳负值、环整不动。放 SysTick(会抢占主循环)
 * 保证固定 5kHz 采样, 远超电机最高换向率(~1k/s@60%PWM), 不混叠。encoder_count() 读到干净计数。 */
/* SysTick 5kHz: 编码器采样 + 递增主时基计数 g_st(200us/拍)。控制调度改读 g_st 算真实时间,
 * 不再数会被 LCD/UART 拖慢的主循环拍数(见时基修正说明)。 */
void SysTick_Handler(void)
{
    encoder_poll();
    /* 相机的 17 字节突发需要 <=350us 的抽取节拍, 200us 的这里是唯一够快又已被验证稳定的地方。
     * 完整理由(含"为什么不开 UART RX 中断")见 vis_drain_isr 上方那段。只搬字节, 不解析。 */
    vis_drain_isr(VIS_UART_INST, &g_vis);       /* PB16 = 固件设计位 */
#if CFG_LINE_UART_EN
    vis_drain_isr(LINE_UART_INST, &g_vis1);     /* PB5 = 载板文档位; 该外设全工程没别人读 */
#endif
    g_st++;
}

/* ==== 姿态 / 航向(yaw) ====
 * 职责分工: attitude.c 是**纯算法层**(轴向置换/积分/滤波都在那儿, 已 PC 单测 PASS);
 *   car.c 只做三件"平台相关"的事: ① 选竖直轴 ② 选符号 ③ 用真实 dt 驱动它。
 *
 * 轴向置换: attitude.c 约定 slot2 = 竖直轴(偏航)。真机实测重力不一定落在 Z(天猛星
 *   2026-07-27 实测主要在 +Y), 故用 attitude_axis_map(g_yaw_axis,...) 把竖直轴循环
 *   置换到 slot2(循环置换 det=+1, 不破坏 pitch/roll 手性)。轴=2 时是恒等变换。
 *
 * ★ 为什么做成**运行时变量**而不是编译期宏: 定轴要试 0/1/2 与符号 ±, 编译期宏意味着
 *   最多 3~6 次重烧 —— 而"连续快烧"正是本工程把芯片怼进 lockup 的头号原因(见 SSOT §D2)。
 *   改成串口命令 a/s 后, 定轴全程**一次烧录 + 在线切换**, 定完再回填 config.h 锁死。
 * // 待真机验证: CFG_YAW_AXIS / CFG_YAW_SIGN 的正确值要靠 L1(平放看哪轴≈1000mg)、
 *   L2(转一下看符号)在板上定, 定完回填 config.h。
 */

/* ==== 差速运动学层(车级指令 -> 左右组量) ====
 * 输入: v=线速度(前进为正), w=角速度(左转/逆时针为正)。输出: 左组量 / 右组量。
 *   左组 = v - w ,  右组 = v + w
 * 分组约定(四驱定版方案A "输入并联扇出"): MOTOR_M1 = 左组, MOTOR_M2 = 右组 —— 后轮 M3/M4 的
 *   IN 脚并到前轮的 PWM 网络上, 所以 motor.c 一行不改, 现 2WD 与将来 4WD 用同一套代码。
 * v=0 时左右反向等速 => 原地转弯(半径≈0), 这正是"四轮差速/坦克转向"要的东西。
 * ⚠ 单位随模式: MODE_DRIVE(m6)=PWM%(开环) / MODE_DRIVE_CL(m7)=RPM(各喂一个速度环)。
 * ⚠ 为什么必须有 m7 闭环: 两轮机械不匹配, **同一个 PWM 喂两边走不出直线**, 只有"左右各一个
 *   独立速度环"才能走直 —— 真机实测: 开环左右计数比 1.20~1.24, m7 闭环压到 1.00。
 * 真机通关 2026-07-27(commit 0c0130d/6229ed4, 轮子离地): v30 r0 两轮同向 / v0 r±30 反向且对称 /
 *   m7 v100 r0 -> 每 100ms 左右各 +134 counts 完全同步。
 * ⚠ 仍未验证: **落地**(抓地/打滑/走直线)与 counts->mm 里程标定(见坑库"台架≠能跑的车")。
 * ⚠ 已知待办: 左轮死区高于右轮(15% 时左轮不动、右轮已转) => 低速差速会往一侧偏, 待加死区前馈。
 */
static void car_drive_mix(int v, int w, int *left, int *right)
{
    *left  = v - w;
    *right = v + w;
}

/* 车级指令 (v,w) -> 左右两个速度环 -> PWM。**m7 / m8 / m9 / m10 共用这一份实现。**
 * 抽出来的理由与 stop_all() 完全一样: 这段逻辑原本只在 m7 里, 后来 m8/m9/m10 都要用它 ——
 * 复制四份的结局是"某天只改了其中三份", 而分叉出来的那一份平时看不出问题、只在特定模式下犯错。
 * 目标 0 时强制停 + 清积分: 否则停车指令下积分残留会让轮子 creep。 */
static int drive_breakaway_floor(int motor, int target)
{
    if (motor == 0) {
        return target > 0 ? CFG_DRV_BREAKAWAY_M1_POS : CFG_DRV_BREAKAWAY_M1_NEG;
    }
    return target > 0 ? CFG_DRV_BREAKAWAY_M2_POS : CFG_DRV_BREAKAWAY_M2_NEG;
}

static int drive_ff_step(int motor, int target, float speed, int pid_out)
{
    int floor;

    /* W0 is the compatibility switch: no dynamic floor and no breakaway floor. */
    if (g_drv_dz <= 0 || target == 0) return pid_out;

    floor = g_drv_dz;
    if (fabsf(speed) < CFG_DRV_BREAKAWAY_RPM) {
        int breakaway = drive_breakaway_floor(motor, target);
        if (breakaway > floor) floor = breakaway;
    }

    /* Follow the requested wheel direction, not the instantaneous PID-output sign.
     * Near zero, using PID sign would flip the compensation and recreate the proven hunt bug. */
    if (target > 0 && pid_out < floor)       pid_out = floor;
    else if (target < 0 && pid_out > -floor) pid_out = -floor;
    return clampi(pid_out, -PWM_CAP, PWM_CAP);
}

static void drive_closed_loop(int v, int w, const float spd[2], int out[2])
{
    int l, r;
    car_drive_mix(v, w, &l, &r);
    if (l == 0) { pid_reset(&pid_v[0]); out[0] = 0; }
    else        { out[0] = (int)pid_step(&pid_v[0], (float)l, spd[0]); }
    if (r == 0) { pid_reset(&pid_v[1]); out[1] = 0; }
    else        { out[1] = (int)pid_step(&pid_v[1], (float)r, spd[1]); }

    /* ---- Speed-loop PWM deadzone compensation ----
     * The old additive W raised the whole turn, so W12 made j-90 overshoot worse. This version
     * treats W as a moving-friction floor; only a stopped wheel gets the measured directional
     * breakaway floor. Once speed is nonzero, the larger floor disappears automatically.
     * Target zero still means exact stop + PID reset, so compensation can never create creep.
     * This path is shared by m7/m8/m9/m10; W0 restores the exact pre-feature behavior. */
    out[0] = drive_ff_step(0, l, spd[0], out[0]);
    out[1] = drive_ff_step(1, r, spd[1], out[1]);
}

/* 位置环内环一步(精定位): |位置误差|<=到位死区 -> 停+复位速度PID(防末端抖/creep);
 * 否则 速度PID输出 + 死区前馈(按方向叠 g_pwm_dz, 推电机越过死区, 末端不停短)。
 * 死区前馈=对电机死区非线性的补偿, 让内环在低速命令下也能真的动->位置能收到目标附近。 */
static int pos_inner_step(pid_t *pv, float v_tgt, float v_meas, int32_t pos_err)
{
    int32_t ae = pos_err < 0 ? -pos_err : pos_err;
    if (ae <= (int32_t)g_pos_tol) { pid_reset(pv); return 0; }
    int pc = (int)pid_step(pv, v_tgt, v_meas);
    /* 死区前馈按【位置误差方向】叠(不按速度PID输出符号): 到位死区外误差方向恒定,
     * 前馈不随速度PID在0附近抖动而反复翻号 -> 不再诱发末端震荡(旧版按pc符号翻号把快电机M2搞震)。 */
    if (pos_err > 0) pc += g_pwm_dz;
    else             pc -= g_pwm_dz;
    return pc;
}

int main(void)
{
    SYSCFG_DL_init();
    delay_ms(200);
#if ENC_PROBE
    enc_probe_run();   /* 临时: 编码器分层探针, 永不返回(排查完把 ENC_PROBE 改回 0) */
#endif
    /* ⚠ 临时启动进度打印(2026-07-29): 加了 linesens 之后固件"哑掉"——有线 COM30 与无线 UART3
     * 同时无输出, 而 SWD/烧录一直正常。改中断/改非阻塞发送都没治好, 说明我在瞎猜。
     * 这几行让固件自己报走到哪一步就没声了: 最后出现的 boot 标记 = 卡点的前一步。
     * 排查完删掉(它们只在启动跑一次, 不影响时序)。 */
    uart_dbg_puts("\nboot1 syscfg+delay ok\n");
    GC9A01_Backlight(1);
    GC9A01_Init();
    uart_dbg_puts("boot2 lcd ok\n");
    motor_init();
    magnet_init();                       /* 电磁铁: 占空先归 0 再启动定时器(上电绝不许默认吸合) */
    servo_init();                        /* 转向舵机: 同理先写 0(不出脉冲=limp), 中位未标定前不许输出 */
    ball_apply_cfg();                    /* 滚球层: 把 config.h §7.12 灌进 g_ball(ball.c 不 include config.h) */
    uart_dbg_puts("boot3 motor+mag+servo ok\n");
    linesens_init();                     /* 循迹模块串口: 只设波特率+清缓冲, 不发任何东西(纯监听) */
    uart_dbg_puts("boot4 linesens ok\n");
    encoder_init();
    uart_dbg_puts("boot5 encoder ok\n");
    SysTick_Config(CPUCLK_HZ / ST_HZ);   /* 控制主时基中断(频率见 config.h ST_HZ; 见 SysTick_Handler) */
    int imu_id = imu_init();   /* ICM42688 初始化(2026-07-27 真机验活: WHOAMI=0x47/|a|=0.995g)。读不到则不阻塞主程序 */
    uart_dbg_puts("boot6 systick+imu ok\n");
    g_imu_ok = (imu_id == ICM42688_WHOAMI_VAL);
    /* dt 传 CFG_IMU_MS 只是初值, 每拍会用 SysTick 真实经过时间覆盖 g_att.dt */
    attitude_init(&g_att, (float)CFG_IMU_MS / 1000.0f, CFG_ATT_ALPHA);
    nav_init(&g_nav);          /* 车级导航层: 参数取 config.h §7.5, 含(现为 0 的)里程/转角标定值 */
    uf_init(&g_uf);            /* 视觉帧解析器: 相机(PB5/PB16) 专用 */
    uf_init(&g_uf_inj);        /* 同上, 注入通道(调试口/ESP)专用 —— 分开的理由见其声明处 */
#if CFG_LINE_UART_EN
    /* 循迹算法层: pos 传 NULL ⇒ 按 CFG_LINE_PITCH_MM 等间距铺开(左为正)。
     * ⚠ CFG_LINE_PITCH_MM 现在是 **[估计]12mm**, 必须拿尺量实物后回填 —— 它是横向偏差的标尺,
     *   量错等于整条循迹的增益标定错(而症状会像"PID 怎么调都蛇行")。 */
    /* 装车朝向(X1 在左还是在右)由 `CFG_LINE_X1_ON_LEFT` 一位决定, **不再硬编码 pos[] 数组**
     * —— 这一位已经翻过一次(2026-07-29 在右 → 2026-07-31 用户把模块倒转 180° 变成在左),
     * 每翻一次都去改 8 个字面量的符号既易错又不可在线验。理由/判据见 config.h 该宏处。
     * 先 line_init(NULL) 建好其余状态, 再由 line_pos_apply() 按朝向铺 pos[]。 */
    line_init(&g_line, LF_CH, 0);
    line_pos_apply();
    g_line.kp = CFG_KP_LINE;  g_line.kd = CFG_KD_LINE;
    g_line.w_max = CFG_LINE_W_MAX;  g_line.search_w = CFG_LINE_SEARCH_W;
    g_line.on_thresh = CFG_LINE_ON_THRESH;  g_line.min_contrast = CFG_LINE_MIN_CONTRAST;
    /* ⭐ **数字量输入下白/黑参考直接置好, 开机即 cal=YES** —— 不是偷懒, 是因为标定在这里
     * **不携带任何信息**: IO 方式每通道只有 0/1, 白就是 0、黑就是 1000, 没有逐通道增益可标。
     * (该标的那部分校准在**模块侧硬件**做: 模块有 KEY 键 + 厂家文档的校准步骤。)
     * 而 `line.c` 的标定是为**模拟量**设计的, 若照它原本的仪式做, 会踩这个坑(2026-07-29 离线查出,
     * 省下大量台架时间): 车压线上时只有 1~2 路读黑, 其余 6 路 `ref_b==ref_w==0` ⇒ 对比度 0
     * < `MIN_CONTRAST` ⇒ `line_bad_channel` 报坏 ⇒ **cal 永远 NO、`line_step` 恒返回 NOCAL
     * 拒绝转向**, 而现象看起来像"循迹不工作"。
     * ⇒ 数字量下真正有效的反馈健康门是另外两个, 都还在: ① 帧新鲜度 `fresh`(链路断=当丢线处理)
     *   ② `LINE_LOST`(没有任何通道在线)。**将来若改用模拟量(`$0,1,0#`), 必须删掉这两行并走
     *   G0/G1 真实标定。** */
    for (int i = 0; i < LF_CH; i++) { g_line.ref_w[i] = 0; g_line.ref_b[i] = 1000; }
    g_line.have_w = 1;  g_line.have_b = 1;
    task_init(&g_task);        /* 第2项任务层(按键/计时/到达/安全停) */
    beep_init(&g_beep);
#endif
    g_vs.center_x  = CFG_VS_CENTER_X;   g_vs.tol_px = CFG_VS_TOL_PX;
    g_vs.kp_w      = CFG_KP_VS_W;       g_vs.w_max  = CFG_VS_W_MAX;
    g_vs.area_stop = CFG_VS_AREA_STOP;  g_vs.kp_v   = CFG_KP_VS_V;
    g_vs.v_max     = CFG_VS_V_MAX;      g_vs.v_min  = CFG_VS_V_MIN;

    GC9A01_FillScreen(LCD_BLACK);
    GC9A01_DrawStringCentered(24, "MOTOR CTL", LCD_GREEN, LCD_BLACK, 2);
    GC9A01_DrawStringCentered(60, "ENCODER CNT", LCD_GRAY, LCD_BLACK, 2);   /* 下方两大数=两编码器计数 */

    pid_init(&pid_i[0], gkp[0], gki[0], gkd[0], 0.0f, (float)PWM_CAP);
    pid_init(&pid_i[1], gkp[0], gki[0], gkd[0], 0.0f, (float)PWM_CAP);
    pid_init(&pid_v[0], gkp[1], gki[1], gkd[1], -(float)PWM_CAP, (float)PWM_CAP);
    pid_init(&pid_v[1], gkp[1], gki[1], gkd[1], -(float)PWM_CAP, (float)PWM_CAP);
    pid_init(&pid_p[0], gkp[2], gki[2], gkd[2], -CFG_POS_VOUT_MAX, CFG_POS_VOUT_MAX);   /* 位置->速度目标(RPM) */
    pid_init(&pid_p[1], gkp[2], gki[2], gkd[2], -CFG_POS_VOUT_MAX, CFG_POS_VOUT_MAX);

    uart_dbg_puts("\n[ctl] boot | modes: m0 IDLE / m1 CURR / m2 SPD / m3 POS / m4 OPEN / m5 DUAL(x/y indep)\n");
    uart_dbg_puts("[ctl]              m6 DRV(open, v/r in PWM%) / m7 DRVC(closed, v/r in RPM via speed loops)\n");
    uart_dbg_puts("[ctl]              m8 NAVS(走N mm+航向保持) / m9 NAVT(原地转N度)  <- 用 n/j 直接进, 不必先发 m8/m9\n");
    uart_dbg_puts("[ctl] cmds: t<v> tgt | v<lin> r<ang> drive | p/i/d<x1000> gains | w<%>dz e<cnt>tol(pos精定位) | f<ms> | z stop | ?\n");
    uart_dbg_puts("[ctl] NAV: n<mm> 走直(负=倒车) | j<deg> 原地转(正=左) | c<x100> 标定counts/mm | q<x100> 标定counts/deg\n");
    uart_dbg_puts("[ctl]      m8/m9 下 p/d 改的是航向/转角增益(纯PD无I); 跑完自动打 [nav] 成绩单; 遥测追加 NAV: 字段\n");
    uart_dbg_puts("[ctl] MAG: E0 放 | E1 吸(满占空->自动降额保持) | E<2..100> 直接给占空 | 电流只证通电不证吸住\n");
    uart_dbg_puts("[ctl] VIS: m10 VSRV 视觉伺服(先对准再前进) | V 看视觉链健康度 | 帧格式 $V,id,cx,cy,area*HH\n");
    uart_dbg_puts("[ctl]      不用相机也能测: 往本口发一行 $V,... 再发 m10 (以 $ 开头的行不进命令通道)\n");
    uart_dbg_puts("[ctl] IMU: g dump | k bias-cal(静止2s) | o yaw=0 | a<0|1|2>定轴 s<1|-1>定符号 ; telemetry Y=yaw(0.1deg) W=wz(0.01dps)\n");
    uart_dbg_puts("[ctl] LCD: u0 计数页 / u1 水平仪页(定轴放平用: 挪车让小球进绿环=<=2deg, 黄=<=5, 橙=<=14底线)\n");
    uart_dbg_puts("[ctl]      u2 RUN页(走时/状态/里程/球位) <- 正式测试的唯一观测通道, 见下一行\n");
    /* 这一行是给"正式测试当天照着念"的: 答疑 Q62 明文只许图传工作, 而遥测一关就没有串口可看,
     * 现场很容易忘了 LCD 那条路、把静默当成板子死了。写进 boot banner 是因为它每次上电都在眼前。 */
    uart_dbg_puts("[ctl] 正式测试(Q62 仅许图传): 发 l0 关全部遥测 + u2 看LCD; 事后 l3 恢复双发\n");
#if CFG_LINE_UART_EN
    /* 开机指纹: 有这一行就说明片上是带循迹串口链路的版本(旧固件打不出来)。
     * 嗅探期的用法: 先 `L` 看 rx 有没有在涨(判物理层), 再 `B<baud>` 扫波特率, 最后看 '|' 定帧边界。*/
    uart_dbg_puts("[line] sniff: UART1 TX=PB4 RX=PB5 @");
    uart_dbg_put_int((int)linesens_get_baud());
    uart_dbg_puts(" (模块TX->B05,共地) | L=dump  B<baud>=换速率\n");
#endif
    uart_dbg_puts("[ctl] SAFETY: 运动超时自停 = 静默(按模式, h<ms>可临时改) + 硬上限 ");
    uart_dbg_put_int((int)hardcap_ms());
    uart_dbg_puts("ms(不可绕过); 触发会打 'RUN TIMEOUT'\n");
#if CFG_ESP_UART_EN
    /* 这一行是"板上跑的这版到底有没有无线口"的开机指纹 —— 旧固件不会打它。
     * 无线那头收到这一行本身就等于端到端通了(MCU->UART3->ESP-A->WiFi->ESP-B->PC 的 COM 口)。 */
    uart_dbg_puts("[uart] wireless: UART3 TX=PB2 RX=PB3 @115200 -> ESP-01S | sinks=");
    uart_dbg_put_int((int)uart_dbg_get_sinks());
    uart_dbg_puts(" (l1 wired / l2 wireless / l3 both) | cmd gate ON, mute ");
    uart_dbg_put_int(CMD_MUTE_MS);
    uart_dbg_puts("ms (ESP boot 日志会被拒, 看遥测 rej= 计数)\n");
#endif
    /* ⚠ 这三个标定常数**必须从 config.h 现算**, 不许写成字面量 —— 原文曾硬编码 "ENC_CPR=800",
     * 而真值 2026-07-27 手转重标成 954.75 后那行字符串没跟着改, boot banner 于此变成
     * "看起来权威的错数据"(本仓库最怕的一类: 下个对话拿它反推 rpm/里程会全错)。
     * 打 ×100 是因为 uart_dbg_put_int 只吃整数, 而这两个值都是小数。 */
    uart_dbg_puts("[ctl] enc=4x quad ENC_CPR*100="); uart_dbg_put_int((int)(ENC_CPR * 100.0f));
    uart_dbg_puts(" counts_per_mm*100=");            uart_dbg_put_int((int)(ENC_COUNTS_PER_MM * 100.0f));
    uart_dbg_puts(" | build "); uart_dbg_puts(__DATE__); uart_dbg_puts(" "); uart_dbg_puts(__TIME__); uart_dbg_puts("\n");
    uart_dbg_puts("[imu] init WHOAMI="); uart_dbg_put_int(imu_id);
    uart_dbg_puts(imu_id == ICM42688_WHOAMI_VAL ? " OK(ICM42688)\n" : " 未就绪(接线/供电/片选异常, 用 g 命令复测)\n");
    uart_dbg_puts("[imu] yaw axis="); uart_dbg_put_int(g_yaw_axis);
    uart_dbg_puts(" sign=");          uart_dbg_put_int(g_yaw_sign);
    uart_dbg_puts(" (来自 config.h, // 待真机定轴; 上电后未 k 标定前 yaw 会漂)\n");
    /* 开机就把标定值打出来: 它们默认是 0, 而 0 意味着 `n<mm>` 会被直接拒。开机看一眼,
     * 比事后分析"车怎么只走了几厘米/怎么不肯动"便宜得多。 */
    print_cal();
    print_status();

    int32_t lastc[2] = { 0, 0 };
    int32_t last_disp[2] = { (int32_t)0x80000000, (int32_t)0x80000000 };  /* LCD 上次显示值, 置不可能值->强制首帧刷 */
    float   speed_rpm[2] = { 0.0f, 0.0f };
    float   v_target[2]  = { 0.0f, 0.0f };   /* 位置环产出的速度目标 */
    int     pwm_out[2]   = { 0, 0 };
    int32_t pos_ref[2]   = { 0, 0 };          /* 位置模式入模零点(相对定位) */
    int     prev_mode    = MODE_IDLE;
    uint32_t last_spd = 0, last_pos = 0, last_prn = 0, last_dsp = 0;   /* 各周期上次触发时刻(g_st单位=200us) */
    uint32_t last_imu = 0;
    uint32_t last_ball = 0;   /* 滚球回路自己的节拍(CFG_BALL_MS), **不再跟 SPEED_MS 共用** */

    while (1) {
#if CFG_ESP_UART_EN
        /* 桥接优先: 在桥接期内只搬字节, 跳过命令解析/控制/遥测/LCD(否则遥测会灌进 AT 通道)。
         * 已在进入时 stop_all() + g_mode=IDLE, 且控制环本就在 SysTick 里 ⇒ 这里 continue 是安全的。 */
        if (g_bridge_end) {
            if ((int32_t)(g_st - g_bridge_end) >= 0) {
                g_bridge_end = 0;
                uart_dbg_puts("\n[uart] bridge END -> normal (telemetry back)\n");
            } else {
                bridge_pump();
                continue;
            }
        }
#endif
        poll_uart();
        uint32_t now = g_st;   /* 真实时基快照(SysTick 5kHz); 编码器采样在同一 ISR */
        /* 电磁铁自己的两件事: 吸合->保持的降额, 与通电总时长上限(防闷烧)。
         * 放在这里而不是塞进 switch: 电磁铁与运动模式**无关** —— 车可以一边 IDLE 一边吸着球。 */
        magnet_tick(now / ST_PER_MS);

        int32_t c0 = encoder_count(ENC_1);
        int32_t c1 = encoder_count(ENC_2);

#if CFG_LINE_UART_EN
        /* ==== 任务层节拍(H题第2项) ====
         * 刻意放在 switch(g_mode) **之前、且不受 g_mode 限制**, 理由三条:
         *   ① 按键要能在 IDLE 下被按到(它就是"启动");
         *   ② 被 RUN TIMEOUT 踢回 IDLE 时任务层也要能自己收尾, 而不是冻在 RUN 上;
         *   ③ 蜂鸣器/走时与运动模式无关(车停了也要显示总时间)。
         * ⚠ 按键低有效: 内部上拉 + 按键接地 ⇒ 读 0 = 按下 ⇒ 这里取反。 */
        {
            uint32_t ms = now / ST_PER_MS;
            task_in_t ti; task_out_t to;
            float dist_mm = ((float)(c0 + c1) * 0.5f) / ENC_COUNTS_PER_MM;
            ti.now_ms    = ms;
            /* 按键有两条来源, 或起来:
             *   ① 真实按键(低有效: 内部上拉 + 按键接地 ⇒ 读 0 = 按下) —— 接线后把 CFG_TASK_HW_EN 改 1
             *   ② **串口虚拟按键 `K`** —— 按键还没接线时用它把整条任务链验完
             *      (同 vision_test.ps1"用 PC 假装相机"那一招: 缺一个器件不该挡住上层验证)
             * 虚拟按下持续 60ms > 消抖窗 20ms, 然后自动松开 ⇒ 一条命令 = 一次干净的按压。 */
            ti.btn = 0;
#if CFG_TASK_HW_EN
            if (!rd(GPIO_TASK_PORT, GPIO_TASK_BTN_PIN)) ti.btn = 1;
#endif
            if (g_btn_virt_until && (int32_t)(ms - g_btn_virt_until) < 0) ti.btn = 1;
            ti.line_lost = (g_line_st == LINE_LOST) ? 1 : 0;
            /* ⚠ **不要写回 `g_line_st == LINE_CROSS`** —— 那要求 8 路全在线, 而启停线只有 50mm
             * 宽、阵列 96mm ⇒ 永不成立(2026-07-29 真机: 跑 7120mm 一次没触发, 最后以 40s
             * TIMEOUT 收场)。判据与三层防误触见 config.h §7.8 的"启停线判据"块。 */
            ti.line_cross= g_cross_on;
            ti.dist_mm   = dist_mm;
            /* "停住了"= 两轮速度都接近 0。用速度环已经算好的 rpm, 不再另起一套判据。 */
            ti.stopped   = (speed_rpm[0] > -3.0f && speed_rpm[0] < 3.0f &&
                            speed_rpm[1] > -3.0f && speed_rpm[1] < 3.0f) ? 1 : 0;
            task_step(&g_task, &ti, &to);

            /* 起跑: 自动进 m11 + 切 RUN 页 + 清本趟统计。**按键就是启动**(说明 5), 不用再发命令。 */
            if (to.beep == TASK_BEEP_START) {
                g_lost_seg = 0; g_was_lost = 0; g_task_run0 = now;
                g_cross_run = 0; g_cross_on = 0; g_cross_max = 0; g_cross_cnt = 0;  /* 本趟统计清零 */
                /* `g_w_lp` 也必须清 —— 上一趟结束时它可能停在弯道值(40+), 不清则新一趟
                 * 起步头 3~4 拍被判成弯道、按 V_SLOW 起步。起步永远是直线, 从 0 起算才对。 */
                g_w_lp = 0.0f;
                g_mode = MODE_LINE; g_mode_at = now; g_cmd_at = now;
                g_dv = 0; g_dw = 0; reset_all_pid();
                g_disp = 2; g_disp_dirty = 1;
                uart_dbg_puts("\n[task] START -> m11 (循迹一圈; z 或再按一次可急停)\n");
            }
            /* 收尾: 打成绩单 + 回 IDLE。**不调 stop_all()** —— 它会把冻结的走时抹掉(见 stop_all 注释)。 */
            if (to.state == TASK_DONE || to.state == TASK_ABORT) {
                if (g_mode == MODE_LINE) {
                    g_mode = MODE_IDLE; g_dv = 0; g_dw = 0; g_target = 0;
                    reset_all_pid();
                    print_task();
                }
            }
            if (to.beep != TASK_BEEP_NONE) {
                beep_req(&g_beep, (beep_pat_t)to.beep, ms);   /* 两个枚举同序, 见 beep.h 注释 */
                /* 蜂鸣器没接线时这一行就是它的替身: 至少能证明"该响的时候确实请求了" */
                uart_dbg_puts("[beep] pat="); uart_dbg_put_int((int)to.beep); uart_dbg_puts("\n");
            }
            g_beep_lv = beep_step(&g_beep, ms);
#if CFG_TASK_HW_EN
            if (g_beep_lv) DL_GPIO_setPins(GPIO_TASK_PORT, GPIO_TASK_BUZZER_PIN);
            else           DL_GPIO_clearPins(GPIO_TASK_PORT, GPIO_TASK_BUZZER_PIN);
#endif
            g_task_v    = (int)to.v_mode;
            g_task_disp = to.disp_dirty;
        }
#endif

        /* 进位置模式: 捕获当前计数为零点 -> 目标是"相对入模点位移"(入模=保持当前位, 不驱回boot零点; 大计数也安全) */
        /* Leaving m12 by any path (m command, z, timeout, bridge) must not carry I into the next
         * ball session. Apply the zero command once as well; changing g_mode alone stops ball_step(),
         * so without this the physical beam would remain at its last nonzero pulse. */
        if (prev_mode == MODE_BALL && g_mode != MODE_BALL) {
            ball_abort(&g_ball);
            ball_drive_servo(0.0f);
        }
        if (g_mode == MODE_POSITION && prev_mode != MODE_POSITION) { pos_ref[0] = c0; pos_ref[1] = c1; }
        prev_mode = g_mode;

        /* ---- 运动安全: 两道闸门(语义/取舍见 config.h §7) ----
         * 放在 switch(g_mode) 之前 ⇒ 触发的同一拍里 MODE_IDLE 分支就把 PWM 打成 0, 不多跑一拍。 */
        if (g_mode != MODE_IDLE) {
            uint32_t lim   = run_limit_ms(g_mode);
            uint32_t quiet = (uint32_t)(now - g_cmd_at);    /* 静默了多久 */
            uint32_t held  = (uint32_t)(now - g_mode_at);   /* 在本模式待了多久 */
            const char *why = 0; uint32_t age = 0;
            if (held >= hardcap_ms() * ST_PER_MS)                  { why = "HARDCAP"; age = held;  }
            else if (lim && quiet >= lim * ST_PER_MS)              { why = "SILENCE"; age = quiet; }
            if (why) {
                /* 不静默停机: 打清楚是哪道闸门、在哪个模式、跑了多久 —— 事后归因全靠这一行 */
                uart_dbg_puts("\n[ctl] !! RUN TIMEOUT ("); uart_dbg_puts(why);
                uart_dbg_puts(") in ");   uart_dbg_puts(mode_name[g_mode]);
                uart_dbg_puts(" after "); uart_dbg_put_int((int)(age / ST_PER_MS));
                uart_dbg_puts("ms -> IDLE, all cmds cleared\n");
                stop_all();
                g_mode_at = now; g_cmd_at = now;
                prev_mode = g_mode;
            }
        }

        /* 调度节拍(按真实时间, 不再数会被 LCD/UART 拖慢的主循环拍): 速度窗/环 SPEED_MS, 位置外环 POS_MS */
        int spd_tick = 0, pos_tick = 0;
        float dt_spd_s = 0.0f;   /* 本拍速度窗的真实经过时间(秒), 导航层要用它积分/微分 */
        if ((uint32_t)(now - last_spd) >= SPEED_MS * ST_PER_MS) {
            float dt_ms = (float)(uint32_t)(now - last_spd) / (float)ST_PER_MS;   /* 真实经过 ms(修正5x scaling) */
            float k = 60000.0f / (ENC_CPR * dt_ms);                               /* counts/窗 -> RPM(真实dt) */
            speed_rpm[0] = (float)(c0 - lastc[0]) * k;
            speed_rpm[1] = (float)(c1 - lastc[1]) * k;
            lastc[0] = c0; lastc[1] = c1;
            last_spd = now;
            dt_spd_s = dt_ms / 1000.0f;
            spd_tick = 1;
        }
        if ((uint32_t)(now - last_pos) >= POS_MS * ST_PER_MS) { last_pos = now; pos_tick = 1; }

        /* ---- 滚球回路节拍(CFG_BALL_MS) ----
         * 为什么要独立于 SPEED_MS: SPEED_MS=50ms 是**速度测量窗**的宽度(编码器一窗多少 counts),
         * 它跟已整定锁定的速度环增益绑死, 不能动。但 20Hz 对滚球回路不够:
         * 2.3.3 节的频域校验要求采样 >= 19.7Hz(穿越频率 0.985Hz 的 20 倍) ⇒ 20Hz 只有 1.02 倍余量,
         * 等于没有余量。取 20ms(50Hz) ⇒ 2.5 倍余量, 且**正好等于舵机 PWM 载波周期**
         * (再快也没用: 指令更新快于载波不会变成更快的机械动作)。
         * 相机 30fps ⇒ 每 1.67 拍来一帧新测量, 约 40% 的拍走观测器模型外推。 */
        int   ball_tick  = 0;
        float dt_ball_s  = 0.0f;
        if ((uint32_t)(now - last_ball) >= CFG_BALL_MS * ST_PER_MS) {
            dt_ball_s = (float)(uint32_t)(now - last_ball) / (float)ST_HZ;   /* 真实 dt, 不用常数 */
            last_ball = now;
            ball_tick = 1;
        }

        /* ---- 姿态/航向节拍(CFG_IMU_MS): 读 IMU -> 轴向置换 -> 符号 -> 死区 -> 用真实 dt 积分 ---- */
        if (g_imu_ok && (uint32_t)(now - last_imu) >= CFG_IMU_MS * ST_PER_MS) {
            float dt_s = (float)(uint32_t)(now - last_imu) / (float)ST_HZ;   /* ★真实 dt, 不用常数 */
            last_imu = now;
            imu_raw_t ir; float gd[3], ag[3], gm[3], am[3];
            imu_read_raw(&ir);
            imu_convert(&ir, gd, ag);
            attitude_axis_map(g_yaw_axis, gd, gm);   /* 把竖直轴搬到 slot2(循环置换, det=+1) */
            attitude_axis_map(g_yaw_axis, ag, am);
            gm[2] *= (float)g_yaw_sign;              /* 只翻偏航那一路; pitch/roll 用 slot0/1, 不受影响 */
            if (g_cal_left > 0) {                 /* 标定中: 只采样, 不积分(车必须静止且处于真实行驶姿态) */
                attitude_bias_sample(&g_att, gm);                 /* 映射系: 供 pitch/roll 用 */
                for (int i = 0; i < 3; i++) { g_cal_g[i] += gd[i]; g_cal_a[i] += ag[i]; }  /* 原始系 */
                if (--g_cal_left == 0) {
                    attitude_bias_apply(&g_att);
                    attitude_reset_yaw(&g_att, 0.0f);
                    /* ★ 同一次静止标定顺便定出"天顶方向": 加速度均值归一化。
                     * 这一步取代了原来的"挑最近轴"——板子装歪也不影响(见 attitude.h)。 */
                    float inv = 1.0f / (float)CFG_IMU_CAL_N;
                    for (int i = 0; i < 3; i++) { g_cal_g[i] *= inv; g_cal_a[i] *= inv; }
                    float am = sqrtf(g_cal_a[0]*g_cal_a[0] + g_cal_a[1]*g_cal_a[1] + g_cal_a[2]*g_cal_a[2]);
                    int amg = (int)(am * 1000.0f + 0.5f);
                    for (int i = 0; i < 3; i++) g_gb_raw[i] = g_cal_g[i];
                    if (amg >= CFG_LEVEL_A_MIN_MG && amg <= CFG_LEVEL_A_MAX_MG) {
                        for (int i = 0; i < 3; i++) g_up[i] = g_cal_a[i] / am;
                        g_up_valid = 1;
                    } else {
                        g_up_valid = 0;   /* |a| 不合理(在动/振动) -> 不敢用, 回落挑轴法 */
                    }
                    for (int i = 0; i < 3; i++) { g_cal_g[i] = 0.0f; g_cal_a[i] = 0.0f; }
                    uart_dbg_puts("[imu] cal done | bias0.01dps(raw): ");
                    uart_dbg_put_int((int)(g_gb_raw[0]*100)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_gb_raw[1]*100)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_gb_raw[2]*100));
                    uart_dbg_puts(" | up0.001: ");
                    uart_dbg_put_int((int)(g_up[0]*1000)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_up[1]*1000)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_up[2]*1000));
                    uart_dbg_puts(" |a|mg="); uart_dbg_put_int(amg);
                    uart_dbg_puts(g_up_valid ? " -> YAW=PROJ(投影, 与装歪无关)\n"
                                             : " -> |a|异常, YAW=AXIS(回落挑轴法), 静止后重发 k\n");
                }
            } else {
                /* 死区作用在"去零偏后"的角速度上。attitude_update 内部会再减一次 gbias,
                 * 所以这里把死区结果加回 gbias 传进去 —— 等效于"先去偏->死区->再积分",
                 * 且 attitude.c(PC 单测过的算法)一行不用改。 */
                /* ★ 偏航角速度: 优先用"原始角速度去零偏后投影到天顶方向"(与 IMU 装歪无关);
                 * 未标定过(g_up_valid=0)才回落到"挑轴+置换"的老路径。 */
                float wz;
                if (g_up_valid) {
                    float gc[3] = { gd[0]-g_gb_raw[0], gd[1]-g_gb_raw[1], gd[2]-g_gb_raw[2] };
                    wz = attitude_yaw_rate(gc, g_up) * (float)g_yaw_sign;
                } else {
                    wz = gm[2] - g_att.gbias[2];   /* gm[2] 已乘过 g_yaw_sign */
                }
                if (wz < CFG_GYRO_DEADBAND_DPS && wz > -CFG_GYRO_DEADBAND_DPS) wz = 0.0f;
                /* 借道: attitude_update 内部还会减一次 gbias[2], 故这里加回去 => 它拿到的正是 wz。
                 * 好处是 attitude.c(PC 单测过)一行不动, 且 pitch/roll 仍走原来的映射系。 */
                gm[2] = g_att.gbias[2] + wz;
                g_wz_dps = wz;
                g_att.dt = dt_s;                  /* 每拍写真实 dt: yaw 积分对 dt 是 1:1 敏感 */
                attitude_update(&g_att, gm, am);
            }
        }

        switch (g_mode) {
        case MODE_IDLE:
            pwm_out[0] = pwm_out[1] = 0;
            break;

        case MODE_OPEN:                 /* g_target = PWM%(带符号) */
            pwm_out[0] = pwm_out[1] = clampi(g_target, -PWM_CAP, PWM_CAP);
            break;

        case MODE_DUAL: {               /* 调试(m5): 独立驱动两电机(x/y) + 每拍读双电流, 专测通道串扰 */
            uint16_t r0 = 0, r1 = 0;
            motor_read_current_raw(&r0, &r1);
            i_meas[0] = (int)motor_current_ma(r0);
            i_meas[1] = (int)motor_current_ma(r1);
            pwm_out[0] = clampi(g_m1duty, -PWM_CAP, PWM_CAP);
            pwm_out[1] = clampi(g_m2duty, -PWM_CAP, PWM_CAP);
            break; }

        case MODE_DRIVE: {              /* m6: 开环差速. v/r 单位=PWM%. 用来验"差速层+左右分组"对不对(离地即可验) */
            int l, r;
            car_drive_mix(g_dv, g_dw, &l, &r);
            pwm_out[0] = clampi(l, -PWM_CAP, PWM_CAP);   /* 左组 = MOTOR_M1 */
            pwm_out[1] = clampi(r, -PWM_CAP, PWM_CAP);   /* 右组 = MOTOR_M2 */
            break; }

        case MODE_DRIVE_CL:             /* m7: 闭环差速. v/r 单位=RPM, 左右各喂一个已达标的速度环(走直线用这个) */
            if (spd_tick) drive_closed_loop(g_dv, g_dw, speed_rpm, pwm_out);
            break;

#if CFG_LINE_UART_EN
        /* m11: H题第2项 —— 循迹跑一圈。结构上同样"只是 m7 前面加了一个产生 (v,w) 的东西":
         *   八路红外 $D 帧 -> line.c 出横向偏差与转向 w -> 仍旧走两个已达标的速度环。
         *   前进速度 v 由 task.c 的档位决定(line.c 有意不出 v, 见 line.h 注释)。
         * ⇒ 不新增任何驱动路径, PWM 限幅/急停/超时自停全部照旧生效。 */
        case MODE_LINE: {
            if (spd_tick) {
                int dig[LF_CH], raw[LINE_MAX_CH], i, fresh, w = 0;
                float err = 0.0f;
                fresh = lf_get_digital(&g_lf, now / ST_PER_MS, CFG_LINE_MAX_AGE_MS, dig);
                /* 数字位 0 = 在黑线上(协议真值, 见 config.h §7.10) ⇒ 映射成 line.c 的
                 * "黑=1000 / 白=0"。只有两个值也照样能过它的归一化, 只是失去了模拟档的插值精度。 */
                for (i = 0; i < LINE_MAX_CH; i++)
                    raw[i] = (i < LF_CH && fresh && dig[i] == 0) ? 1000 : 0;

                if (!fresh) {
                    /* 拿不到新鲜帧 = **链路断**, 不是"车偏了"。当丢线处理 ⇒ 走 task 的 LOST 闸门
                     * 最终安全停; 绝不拿过期数据继续开(uart_frame.h 立的规矩)。 */
                    g_line_st = LINE_LOST;
                    w = 0;
                } else {
                    g_line_st = (int)line_step(&g_line, raw, dt_spd_s, &err, &w);
                    g_line_err = err;
                    if (g_line_st == LINE_NOCAL) w = 0;   /* 没标定就不许转向(line.c 已保证, 这里再兜一层) */
                }
                /* ==== 启停线判据（在线通道数达门限 + 连续 N 拍）====
                 * 在这里算而不是在 task 输入处算: task 那段每个主循环都跑一次(不按拍),
                 * 在那里数"连续几拍"会数成主循环迭代数 —— 而主循环会被 LCD/UART 拖成变周期,
                 * 门限就失去时间含义了。这里是固定 50ms 的速度窗, 拍数=时间。
                 * `on_mask` 在 line_step 里对 OK/CROSS/LOST/NOCAL 四种返回都已正确填好
                 * (LOST/NOCAL 时为 0) ⇒ 直接数它的置位数即可, 不必分状态。 */
                {
                    int m = g_line.on_mask, n_on = 0;
                    while (m) { n_on += (m & 1); m >>= 1; }
                    g_cross_cur = n_on;
                    if (n_on > g_cross_max) g_cross_max = n_on;
                    if (n_on >= g_cross_min) {      /* 门限走运行时副本(命令 N), 不直读 CFG_ */
                        if (g_cross_run < 1000) g_cross_run++;
                    } else {
                        g_cross_run = 0;
                    }
                    {   /* 只在**上升沿**计数(不是每拍累加) ⇒ xcnt 直接读作"压到几次启停线" */
                        int on = (g_cross_run >= CFG_LINE_CROSS_TICKS) ? 1 : 0;
                        if (on && !g_cross_on) g_cross_cnt++;
                        g_cross_on = on;
                    }
                }
                /* 数丢线**段数**: 用于屏上 L<n> 与成绩单 —— "这趟干净不干净"比"丢了多少毫秒"更好读 */
                if (g_line_st == LINE_LOST) { if (!g_was_lost) { g_lost_seg++; g_was_lost = 1; } }
                else                          g_was_lost = 0;

                g_dw = w;
                /* ==== 分段速度: 直线快 / 弯道慢 ====
                 * 只改"巡航那一档给多少 v", **不碰速度环本身**(它是达标锁死的)。
                 * 曲率代理 = |w| 的低通(理由与"为什么不用 |err|"写在 config.h §7.8)。
                 * 注意顺序: 用的是**本拍刚算出来的 w**, 所以降速与转向同拍生效、不滞后一拍。
                 * 任务层的 SLOW 档(接近终点预降速)优先级更高 —— 它是为刹车距离服务的, 不该被覆盖。 */
                {
                    float wa = (float)((w < 0) ? -w : w);
                    g_w_lp += CFG_LINE_W_LP_A * (wa - g_w_lp);
                }
                if (g_task_v == 2) {
                    g_dv = CFG_TASK_V_SLOW;                 /* 终点预降速优先, 不参与分段 */
                } else if (g_task_v == 1) {
                    int v_fast = (g_line_v_cruise > 0) ? g_line_v_cruise : CFG_TASK_V_CRUISE;
#if CFG_LINE_VSEG_EN
                    /* 用 g_vs_* 运行时副本(命令 A/H/D 可在线改), 不直读 CFG_ —— 否则整定要重烧 */
                    int v_curve = g_vs_slow;
                    if (v_curve > v_fast) v_curve = v_fast;  /* 弯道档不该比直线档还快 */
                    if (g_w_lp <= g_vs_lo) {
                        g_dv = v_fast;
                    } else if (g_w_lp >= g_vs_hi) {
                        g_dv = v_curve;
                    } else {
                        /* 线性插值, **不跳档** —— 跳档会在过渡区反复切换, 把速度环激成方波。
                         * 分母 (hi-lo) 由 vseg_cmd 入口保证 >=1.0, 这里不必再判 0。 */
                        float t = (g_w_lp - g_vs_lo) / (g_vs_hi - g_vs_lo);
                        g_dv = v_fast - (int)(t * (float)(v_fast - v_curve));
                    }
#else
                    g_dv = v_fast;
#endif
                } else {
                    g_dv = 0;
                }
                g_vseg_now = g_dv;
                if (g_dv != 0) drive_closed_loop(g_dv, g_dw, speed_rpm, pwm_out);
                else           { pwm_out[0] = pwm_out[1] = 0; }
            }
            break; }
#endif

        /* m8 走 N mm(带航向保持) / m9 原地转 N 度 —— 车级导航(阶梯 2.5/3/4)。
         * 结构上它只是"m7 前面加了一个产生 (v,w) 的东西": 导航层出车级指令 -> 仍旧走左右两个
         * 已达标的速度环。所以这里**不新增任何驱动路径**, PWM 限幅/急停/超时自停全部照旧生效。
         * 节拍用速度窗(SPEED_MS=50ms/20Hz): 导航输出的 v/w 正是速度环的目标, 同拍更新最自然,
         * 且 dt 传的是**真实经过时间**(yaw 积分与 D 项对 dt 都是 1:1 敏感, 本工程为此吃过 5x 的亏)。 */
        case MODE_NAV_S:
        case MODE_NAV_T: {
            if (spd_tick) {
                nav_in_t in;
                in.counts_l   = (long)c0;
                in.counts_r   = (long)c1;
                in.yaw_deg    = g_att.yaw;
                in.wz_dps     = g_wz_dps;
                in.rpm_avg    = 0.5f * (speed_rpm[0] + speed_rpm[1]);
                in.dt_s       = dt_spd_s;
                /* 航向可信 = IMU 在 且 做过 `k`(拿到天顶向量)。只有 g_imu_ok 不够 —— 没标定的陀螺
                 * 有零偏, 拿它纠偏等于主动把车拐歪(比不纠偏更坏), 所以这里要的是 g_up_valid。 */
                in.heading_ok = (g_imu_ok && g_up_valid) ? 1 : 0;
                int nv = 0, nw = 0;
                nav_state_t st = nav_step(&g_nav, &in, &nv, &nw);
                g_dv = nv; g_dw = nw;      /* 借道 D: 遥测字段 => 不加新字段就能看见导航在要什么 */
                if (st == NAV_RUN) {
                    drive_closed_loop(g_dv, g_dw, speed_rpm, pwm_out);
                } else if (st != NAV_IDLE) {
                    /* 到位 或 干不下去: 先打成绩单再停(停会清掉状态, 顺序不能换), 走 z 同一条路径 */
                    nav_report();
                    stop_all();
                    pwm_out[0] = pwm_out[1] = 0;
                } else {
                    pwm_out[0] = pwm_out[1] = 0;   /* 只发了 m8/m9 但没给目标: 安全地什么都不做 */
                }
            }
            break; }

        /* m10 反应式视觉伺服(阶梯 6): 看到目标 -> 先转到画面中心 -> 再直着开过去 -> 到位停。
         * "先对准再前进"是有意的: 同时做需要相机几何标定(FOV/安装角/目标真实尺寸), 赛场标不起;
         * 分两步只需要一个符号正确的比例项。理由与代价见 vservo.h。
         * 帧从现有串口进来(poll_uart 的 '$' 分流) ⇒ **不用相机也能测**: PC 发帧即可。 */
        case MODE_VSERVO: {
            if (spd_tick) {
                uf_target_t t;
                t.id = 0; t.cx = 0; t.cy = 0; t.area = 0; t.stamp_ms = 0;
                /* uf_get 只在"新鲜 且 确实有目标"时返 1 ⇒ 相机掉线后车不会按旧坐标继续冲 */
                int have = uf_get(&g_uf, now / ST_PER_MS, &t);
                int nv = 0, nw = 0;
                vs_state_t vst = vs_step(&g_vs, have, (int)t.cx, (int)t.area, &nv, &nw);
                g_dv = nv; g_dw = nw;
                if (vst == VS_LOST) {
                    /* 丢目标先停着等(相机偶发丢帧很常见), 但不能无限等 —— 超时就退出模式并说清原因,
                     * 否则现象是"车停在场地中间不动", 而人根本不知道是相机没数据还是固件卡了。 */
                    g_vs_lost_ms += (uint32_t)(dt_spd_s * 1000.0f);
                    pwm_out[0] = pwm_out[1] = 0;
                    if (g_vs_lost_ms >= (uint32_t)CFG_VS_LOST_MS) {
                        uart_dbg_puts("\n[vs] TARGET LOST ");
                        uart_dbg_put_int((int)g_vs_lost_ms);
                        uart_dbg_puts("ms -> IDLE | status=");
                        uart_dbg_put_int((int)uf_status(&g_uf, now / ST_PER_MS));
                        uart_dbg_puts(" (0=OK 1=NO_DATA 2=STALE 3=NO_TARGET; 用 V 命令看计数)\n");
                        stop_all();
                    }
                } else {
                    g_vs_lost_ms = 0;
                    if (vst == VS_ALIGNED) {
                        /* 对准且够近: 停下, 把"动手"(吸球 E1 / 投放 E0)留给上层决定 —— 固件不替人做 */
                        uart_dbg_puts("\n[vs] ALIGNED cx="); uart_dbg_put_int((int)t.cx);
                        uart_dbg_puts(" area=");             uart_dbg_put_int((int)t.area);
                        uart_dbg_puts(" -> 停车, 可以动手(E1 吸)\n");
                        stop_all();
                        pwm_out[0] = pwm_out[1] = 0;
                    } else {
                        drive_closed_loop(g_dv, g_dw, speed_rpm, pwm_out);
                    }
                }
            }
            break; }

        /* m12: H题第3项 —— 车静止, 摆杆把球稳在目标位 / 跑 O->+5cm->折返->-5cm 的往返轨迹。
         * ⭐ 球位帧走的是**现有**那条 '$' 分流(poll_uart 把整行喂 g_uf) ⇒ **不用相机也能测**:
         *   PC 发 `$V,1,<x_mm*100>,0,0*<异或>` 即可(tools/vision_test.ps1)。这正是"摆杆和相机
         *   都还没到位、软件先在板上验完"的那条路 —— 摆杆一到位只剩装 + 标定 + 整定。
         * ⚠ 本模式**强制 pwm=0**: 要求3 明文"小车在静止状态时"。 */
        case MODE_BALL: {
            if (ball_tick) {
                uf_target_t t;
                ball_in_t   bin;
                float th = 0.0f;
                uint32_t ms = now / ST_PER_MS;
                int have, fresh = 0;

                t.id = 0; t.cx = 0; t.cy = 0; t.area = 0; t.stamp_ms = 0;
                have = uf_get(&g_uf, ms, &t);
                if (have) {
                    /* 只有 stamp 变了才算"新测量" —— 同一帧被多拍反复读到, 不能当新数据喂观测器
                     * (相机 30fps 而本回路 50Hz ⇒ 约三分之一的拍走模型外推, 见 ball.h) */
                    if (!g_ball_stamp_init || t.stamp_ms != g_ball_stamp) {
                        fresh = 1; g_ball_stamp = t.stamp_ms; g_ball_stamp_init = 1;
                    }
                }
                bin.x_mm       = (float)t.cx / CFG_BALL_CX_PER_MM;
                bin.meas_valid = fresh;
                /* 从没收到过帧就给个大龄, 让 ball 判 NO_MEAS 并摊平摆杆 —— 不能拿 0 当"球在中心" */
                bin.meas_age_s = g_uf.have_frame ? ((float)(ms - g_uf.last.stamp_ms) / 1000.0f)
                                                 : 999.0f;
                /* a_x 用**车级速度指令的微分**而不是 IMU: 前馈要的是"即将发生的加速度",
                 * 而 IMU 测的是"已经发生的", 后者天生晚一拍(取舍写在 ball.h)。
                 * m12 下车不动 ⇒ g_dv 恒 0 ⇒ a_x=0; 代码提前写好, 将来并进 m11 直接可用。 */
                {
                    float mm_per_rev = ENC_CPR / ENC_COUNTS_PER_MM;
                    float v_mm_s = (float)g_dv / 60.0f * mm_per_rev;
                    float ax = (dt_ball_s > 0.0f) ? (v_mm_s - g_ball_v_prev) / dt_ball_s : 0.0f;
                    g_ball_v_prev = v_mm_s;
                    bin.ax_mm_s2 = (float)CFG_BALL_AX_SIGN * ax;
                }
                bin.pitch_deg = (float)CFG_BALL_PITCH_SIGN * g_att.pitch;
                bin.dt_s      = dt_ball_s;

                ball_step(&g_ball, &bin, &th);
                ball_drive_servo(th);
                pwm_out[0] = pwm_out[1] = 0;
            }
            break; }

        case MODE_CURRENT: {            /* g_target = 目标电流 mA(带符号=方向) */
            uint16_t r0 = 0, r1 = 0;
            motor_read_current_raw(&r0, &r1);
            i_meas[0] = (int)motor_current_ma(r0);
            i_meas[1] = (int)motor_current_ma(r1);
            int mag = g_target < 0 ? -g_target : g_target;
            int dir = g_target > 0 ? 1 : (g_target < 0 ? -1 : 0);
            if (dir == 0) { pid_reset(&pid_i[0]); pid_reset(&pid_i[1]); pwm_out[0] = pwm_out[1] = 0; }
            else {
                pwm_out[0] = dir * (int)pid_step(&pid_i[0], (float)mag, (float)i_meas[0]);
                pwm_out[1] = dir * (int)pid_step(&pid_i[1], (float)mag, (float)i_meas[1]);
            }
            break; }

        case MODE_SPEED:                /* g_target = 目标转速 RPM(带符号) */
            if (spd_tick) {
                pwm_out[0] = (int)pid_step(&pid_v[0], (float)g_target, speed_rpm[0]);
                pwm_out[1] = (int)pid_step(&pid_v[1], (float)g_target, speed_rpm[1]);
            }
            break;

        case MODE_POSITION: {           /* g_target = 目标位置 counts(相对入模点) */
            int32_t rel0 = c0 - pos_ref[0], rel1 = c1 - pos_ref[1];
            if (pos_tick) {             /* 外环: 相对位置->速度目标 */
                v_target[0] = pid_step(&pid_p[0], (float)g_target, (float)rel0);
                v_target[1] = pid_step(&pid_p[1], (float)g_target, (float)rel1);
            }
            if (spd_tick) {             /* 内环: 速度环 + 死区前馈 + 到位死区(精定位) */
                pwm_out[0] = pos_inner_step(&pid_v[0], v_target[0], speed_rpm[0], g_target - rel0);
                pwm_out[1] = pos_inner_step(&pid_v[1], v_target[1], speed_rpm[1], g_target - rel1);
            }
            break; }
        }

        pwm_out[0] = clampi(pwm_out[0], -PWM_CAP, PWM_CAP);
        pwm_out[1] = clampi(pwm_out[1], -PWM_CAP, PWM_CAP);
        motor_set(MOTOR_M1, (int16_t)pwm_out[0]);
        motor_set(MOTOR_M2, (int16_t)pwm_out[1]);

        if ((uint32_t)(now - last_prn) >= (uint32_t)g_print_ms * ST_PER_MS) {
            last_prn = now;
            uart_dbg_puts("[ctl] "); uart_dbg_puts(mode_name[g_mode]);
            uart_dbg_puts(" tgt=");  uart_dbg_put_int(g_target);
            uart_dbg_puts(" | I:"); uart_dbg_put_int(i_meas[0]); uart_dbg_putc(','); uart_dbg_put_int(i_meas[1]);
            uart_dbg_puts(" | V:"); uart_dbg_put_int((int)speed_rpm[0]); uart_dbg_putc(','); uart_dbg_put_int((int)speed_rpm[1]);
            uart_dbg_puts(" | PWM:"); uart_dbg_put_int(pwm_out[0]); uart_dbg_putc(','); uart_dbg_put_int(pwm_out[1]);
            uart_dbg_puts(" | C:"); uart_dbg_put_int(c0); uart_dbg_putc(','); uart_dbg_put_int(c1);
            /* 车级指令(v,w) 追加在行尾: 现有脚本按 "V:"/"PWM:"/"C:" 取值, 末尾加 "D:" 字段不破它们的解析 */
            uart_dbg_puts(" | D:"); uart_dbg_put_int(g_dv); uart_dbg_putc(','); uart_dbg_put_int(g_dw);
            /* 导航进度 NAV:<state>,<剩余mm>,<剩余角×10>,<峰值航向偏差×10>
             *   state: 0=IDLE 1=RUN 2=DONE 3=BLOCKED
             * **只在导航模式下追加**: 平时一个字节不多花(f20 双发时每行 15.4ms, 字节是有代价的),
             * 而正在跑导航时它恰好是最想看的。现有脚本的正则字段之间是 `.*?`, 追加字段不破解析。 */
            if (g_mode == MODE_NAV_S || g_mode == MODE_NAV_T) {
                uart_dbg_puts(" | NAV:"); uart_dbg_put_int((int)g_nav.state);
                uart_dbg_putc(',');       uart_dbg_put_int((int)g_nav.err_mm);
                uart_dbg_putc(',');       uart_dbg_put_int((int)(g_nav.err_deg * 10.0f));
                uart_dbg_putc(',');       uart_dbg_put_int((int)(g_nav.peak_hdg_deg * 10.0f));
            }
            /* 摆杆/球标定字段 BALL:<cx>,<us>,<age_ms>,<stamp_ms>
             * **只要视觉链曾收到过帧就打, 不限模式** —— 这是刻意的: 摆杆标定(tools/ball_ident.ps1)
             *   是在 IDLE 下用 `U<us>` 直接给脉宽、让球自由滚, 此时 ball 环没跑, 若只在 m12 打就读不到。
             * ⭐ 为什么打**原始像素 cx** 而不是换算好的 mm:
             *   CFG_BALL_CX_PER_MM 是标定的**产物**之一(现在那个 100.0f 是猜的)。打 mm 就等于把一个
             *   未标定的常数烙进数据里 ⇒ 每改一次标尺都要重烧板。打 cx 则 PC 侧可任意重标、零重烧,
             *   而"少烧一次板"在本工程是有价格的(禁忌 2: 连续快烧曾把芯片怼进 lockup)。
             * stamp_ms 是给 PC 判"这帧是不是新的": 相机掉线时 age 会涨而 stamp 冻住, 两者一起看
             *   才能区分"相机没更新"与"MCU 没在读" —— 只看 age 分不出来。 */
            if (g_uf.have_frame) {
                uint32_t bms = now / ST_PER_MS;
                uart_dbg_puts(" | BALL:"); uart_dbg_put_int((int)g_uf.last.cx);
                uart_dbg_putc(',');        uart_dbg_put_int(servo_us());
                uart_dbg_putc(',');        uart_dbg_put_int((int)(bms - g_uf.last.stamp_ms));
                uart_dbg_putc(',');        uart_dbg_put_int((int)g_uf.last.stamp_ms);
                /* ⚠ id 必须一起打, 不是可选字段。相机没看到球时发的是 `$V,-1,0,0,0` —— 那**是一个
                 * 有效帧**(uart_frame.c 刻意如此: 用来区分"模块活着但没看见"与"模块掉线"), 所以
                 * stamp 会更新而 cx 是 0。只看 cx 的话, "没看到球"会被读成"球正好在中心 0mm",
                 * 而标定脚本按 stamp 变化收样本 ⇒ 会把一串假的 0 喂进抛物线拟合。
                 * ⇒ PC 侧必须先按 id != -1 过滤。判据: 遮住相机, id 应立刻变 -1 而 age 仍很小。 */
                uart_dbg_putc(',');        uart_dbg_put_int((int)g_uf.last.id);
            }
            /* 观测器内部量 BE:<x_est×10>,<v_est×10>,<x_ref×10>,<th_cmd×10>,<sat>,<peak×10>
             * 只在 m12 追加(字节在无线下有代价: 双发实测 +6.7ms/行)。整定时要看的就是这几个:
             *   x_est vs x_ref 看跟得上没, th_cmd 看有没有一直贴着限幅(sat=1 说明增益或轨迹过激),
             *   peak 是**判分量**(评委看回放取最坏帧, 不是看平均)。 */
            if (g_mode == MODE_BALL) {
                uart_dbg_puts(" | BE:"); uart_dbg_put_int((int)(g_ball.x_est * 10.0f));
                uart_dbg_putc(',');      uart_dbg_put_int((int)(g_ball.v_est * 10.0f));
                uart_dbg_putc(',');      uart_dbg_put_int((int)(g_ball.x_ref_mm * 10.0f));
                uart_dbg_putc(',');      uart_dbg_put_int((int)(g_ball.theta_cmd_deg * 10.0f));
                uart_dbg_putc(',');      uart_dbg_put_int(g_ball.sat);
                uart_dbg_putc(',');      uart_dbg_put_int((int)(g_ball.peak_abs_err_mm * 10.0f));
            }
            /* 航向: Y=yaw(0.1°, 连续累计可超±360, 便于"转5圈"标度校验) W=偏航角速度(0.01dps) */
            uart_dbg_puts(" | Y:"); uart_dbg_put_int((int)(g_att.yaw * 10.0f));
            uart_dbg_puts(" W:");   uart_dbg_put_int((int)(g_wz_dps * 100.0f));
            /* 行尾两个固定字段(无线场景的刚需, 有线时也有用):
             *   #<seq> = 单调递增的遥测行号   t<ms> = 固件端时间戳(上电毫秒)
             * 为什么必须有 seq: 无线走 UDP、无重传, 跑出范围那段遥测**永久消失**;
             *   没有行号就分不清"这段没数据"到底是**丢包**还是**车真的没动** —— 那是分析数据时最要命的歧义。
             *   有了它, 丢包率 = 收到行数 ÷ (末seq − 首seq + 1), 是真丢包率而非"行数对不对得上期望"。
             * 为什么时间戳要**固件端**出: PC 端收到的时刻含"成包突发"(实测单行间隔 0~187ms、6.9% 挤在同一
             *   UDP 包里齐到) ⇒ PC 时间戳只能看速率趋势、算不出精确 dt。固件端 t 是真实节拍。
             * 为什么放行尾: 现有 6 个 .ps1 都按字段名正则取值, 追加尾字段不破解析(与 D:/Y: 同理)。 */
            uart_dbg_puts(" | #"); uart_dbg_put_int((int)(++g_tele_seq));
            uart_dbg_puts(" t");   uart_dbg_put_int((int)(now / ST_PER_MS));
            if (g_cal_left > 0) uart_dbg_puts(" CAL");
            uart_dbg_puts("\n");
        }

        /* LCD 计数刷新: 仅在计数变化时重绘对应行(静止零刷=不闪烁),
         * 固定宽度右对齐 + 不透明黑底 => 覆盖式绘制, 免 FillRect 清屏、无残影、位置不漂。 */
        /* 刷新周期按页分: RUN 页要实时走时(说明 5)所以快; 计数页很贵(逐字符 set_window)所以慢。
         * 本工程吃过"LCD 拖慢主循环 ⇒ RPM 虚高 5x"的亏(`4bbaae5`), 这一行就是那笔账的落地。 */
#if CFG_LINE_UART_EN
        uint32_t dsp_period = (g_disp == 2) ? (uint32_t)CFG_DISP_RUN_MS : (uint32_t)DISP_MS;
#else
        uint32_t dsp_period = (uint32_t)DISP_MS;
#endif
        if ((uint32_t)(now - last_dsp) >= dsp_period * ST_PER_MS) {
            last_dsp = now;
            /* 水平仪页只在 IDLE 允许(它比计数页重得多) —— 一进运动模式自动切回,
             * 免得"为了看屏"把控制环的时基又拖歪(本工程为此吃过 RPM 虚高 5x 的亏)。 */
            if (g_disp == 1 && g_mode != MODE_IDLE) {
                g_disp = 0; g_disp_dirty = 1;
                uart_dbg_puts("[lcd] 进运动模式 -> 自动切回计数页(水平仪页仅 IDLE 可用)\n");
            }
#if CFG_LINE_UART_EN
            /* RUN 页: 走时/状态/失败原因/里程/球位/跑次+丢线段数。
             * **跑完不自动切走** —— 那一屏就是"停车后显示总时间"(说明 5), 也是脱缆跑完把车捡回来
             * 念数/拍照的唯一通道(Q62 测试期间只许图传工作 ⇒ 那时没有遥测)。要切回按 u0。 */
            if (g_disp == 2) {
                disp_run_in_t di; disp_run_txt_t cur; uint32_t dm; int li;
                static disp_run_txt_t s_prev; static int s_first = 1;
                static const int16_t RY[DISP_RUN_LINES] = { 96, 40, 66, 140, 166, 202 };
                if (g_disp_dirty) { g_disp_dirty = 0; s_first = 1;
                                    GC9A01_FillScreen(LCD_BLACK);
                                    GC9A01_DrawStringCentered(14, "LAP RUN", LCD_GRAY, LCD_BLACK, 2); }
                di.state      = g_task.state;
                di.fail       = g_task.fail;
                di.elapsed_ms = (g_task.state == TASK_IDLE) ? 0u
                              : (g_task.state == TASK_DONE || g_task.state == TASK_ABORT)
                                    ? (g_task.t_stop - g_task.t_start)
                                    : (now / ST_PER_MS - g_task.t_start);
                di.dist_mm    = ((float)(c0 + c1) * 0.5f) / ENC_COUNTS_PER_MM;
                /* 🔴 2026-07-31 修: `g_ball_mm` 此前**全工程只有声明处那一次初始化**(DISP_RUN_NO_BALL),
                 * 没有任何地方给它赋过值 ⇒ RUN 页的球位那一行**永远显示 "ball --"**。
                 * 为什么这是硬伤而不是小瑕疵: 官方答疑 Q62 要求正式测试期间只许图传工作 ⇒ 那时
                 *   **LCD 是唯一观测通道**, 而第 3/4/5/6 项全部是球位判据 ⇒ 我们自己看不到球在哪。
                 * 这里就地取值而不是在 m12 里赋值, 有两个理由:
                 *   ① **不限模式** —— IDLE 下也要能看到球位: 标定时确认球放进槽了、正式测试按键前确认
                 *      相机已就绪(K230 有 3.5MB kmodel, 上电到出帧要多久还没测), 都靠这一行;
                 *   ② 单一赋值点, 不会出现"m12 更新了、别的模式留着陈旧值"那类不一致。
                 * 新鲜度判据直接用 uf_get(): 它只在"新鲜 且 确实有目标(id!=-1)"时返 1 ⇒ 相机掉线或
                 *   视野里没球, 这里就退回 NO_BALL 显示 "ball --", **绝不显示陈旧坐标**
                 *   (显示陈旧值比显示"没有"危险: 人会以为球还在那儿)。 */
                {
                    uf_target_t bt;
                    g_ball_mm = uf_get(&g_uf, now / ST_PER_MS, &bt)
                              ? ((float)bt.cx / CFG_BALL_CX_PER_MM)
                              : DISP_RUN_NO_BALL;
                }
                di.ball_mm    = g_ball_mm;
                di.n_runs     = g_task.n_runs;
                di.n_lost     = g_lost_seg;
                disp_run_build(&di, &cur);
                dm = disp_run_diff(s_first ? 0 : &s_prev, &cur);
                for (li = 0; li < DISP_RUN_LINES; li++) {
                    if (!(dm & (1u << li))) continue;
                    GC9A01_DrawStringCentered(RY[li], cur.line[li],
                        (li == 0) ? LCD_GREEN : (li == 2) ? LCD_RED : LCD_WHITE,
                        LCD_BLACK, (li == 0) ? 3 : 2);
                }
                s_prev = cur; s_first = 0;
                goto disp_done;
            }
#endif
            if (g_disp == 1) {
                if (g_imu_ok) {
                    imu_raw_t lr; float lg[3], la[3];
                    imu_read_raw(&lr); imu_convert(&lr, lg, la);
                    disp_level(la);
                } else if (!g_lv_static) {
                    GC9A01_FillScreen(LCD_BLACK);
                    GC9A01_DrawStringCentered(110, "NO IMU", LCD_RED, LCD_BLACK, 2);
                    g_lv_static = 1;
                }
                goto disp_done;
            }
            if (g_disp_dirty) {        /* 从水平仪页切回来: 重画计数页静态层并强制刷数值 */
                g_disp_dirty = 0;
                GC9A01_FillScreen(LCD_BLACK);
                GC9A01_DrawStringCentered(24, "MOTOR CTL", LCD_GREEN, LCD_BLACK, 2);
                GC9A01_DrawStringCentered(60, "ENCODER CNT", LCD_GRAY, LCD_BLACK, 2);
                last_disp[0] = last_disp[1] = (int32_t)0x80000000;
            }
            char db[16];
            if (c0 != last_disp[0]) {
                db[0] = 'C'; db[1] = '1'; db[2] = '='; i32_to_field(db + 3, c0, 6);
                GC9A01_DrawString(12, 100, db, LCD_GREEN, LCD_BLACK, 3);
                last_disp[0] = c0;
            }
            if (c1 != last_disp[1]) {
                db[0] = 'C'; db[1] = '2'; db[2] = '='; i32_to_field(db + 3, c1, 6);
                GC9A01_DrawString(12, 150, db, LCD_CYAN, LCD_BLACK, 3);
                last_disp[1] = c1;
            }
disp_done: ;
        }

        delay_ms(1);   /* 轻延时防空转; 控制/遥测/LCD 均按 g_st 真实时间调度, 不依赖此延时的精度 */
    }
}
