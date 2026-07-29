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
#include <math.h>       /* 水平仪页用 sqrtf/fabsf/atan2f (软浮点, 仅 IDLE 下的显示页调用) */

/* ★所有可调参数(时基/周期/PWM上限/ENC_CPR/PID增益/死区/容差/调试开关)已集中到 config.h。
 * 本文件只写逻辑, 不再散落 #define —— 赛场调参只翻 config.h(工作台规范 §2 铁律)。
 * 参数的出处/实测依据写在 config.h 每个宏旁边; 事实真值源见 .kiro/steering/工程事实SSOT.md。 */
#include "config.h"

static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

/* ==== 模式 ==== */
enum { MODE_IDLE = 0, MODE_CURRENT, MODE_SPEED, MODE_POSITION, MODE_OPEN, MODE_DUAL,
       MODE_DRIVE, MODE_DRIVE_CL, MODE_NAV_S, MODE_NAV_T, MODE_VSERVO, MODE_N };
static const char *mode_name[MODE_N] = { "IDLE", "CURR", "SPD", "POS", "OPEN", "DUAL", "DRV", "DRVC",
                                        "NAVS", "NAVT", "VSRV" };

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
/* ==== 视觉链（m10 VSRV）====
 * g_uf = 帧解析器(喂字节, 出目标+新鲜度)  g_vs = 伺服控制律参数(来自 config.h §7.7)
 * ★ 帧从**现有两个串口**进来(见 poll_uart 里的 '$' 分流) ⇒ 不用第三路 UART、不用相机就能测:
 *   PC 直接发 `$V,1,200,240,900*HH` 就能让车按"看到目标"去动。相机到手后接哪个口都行。 */
static uf_parser_t g_uf;
static vs_cfg_t    g_vs;
static uint32_t    g_vs_lost_ms = 0;   /* 连续拿不到新鲜目标多久了(ms) */
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
        default:            return 0;
    }
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
    return 0;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
static void print_vision(void)
{
    uint32_t ms = g_st / ST_PER_MS;
    uf_status_t st = uf_status(&g_uf, ms);
    uart_dbg_puts("[vs] status=");
    uart_dbg_puts(st == UF_OK ? "OK" : (st == UF_NO_DATA ? "NO_DATA" :
                 (st == UF_STALE ? "STALE" : "NO_TARGET")));
    uart_dbg_puts(" ok=");        uart_dbg_put_int((int)g_uf.n_ok);
    uart_dbg_puts(" bad_csum=");  uart_dbg_put_int((int)g_uf.n_bad_csum);
    uart_dbg_puts(" bad_form=");  uart_dbg_put_int((int)g_uf.n_bad_form);
    uart_dbg_puts(" overflow=");  uart_dbg_put_int((int)g_uf.n_overflow);
    uart_dbg_puts(" | last id="); uart_dbg_put_int((int)g_uf.last.id);
    uart_dbg_puts(" cx=");        uart_dbg_put_int((int)g_uf.last.cx);
    uart_dbg_puts(" area=");      uart_dbg_put_int((int)g_uf.last.area);
    uart_dbg_puts(" age_ms=");    uart_dbg_put_int((int)(g_uf.have_frame ? (ms - g_uf.last.stamp_ms) : 0));
    uart_dbg_puts("\n[vs] 自测(不用相机): 往本口发一行 $V,1,200,240,900*<异或校验> 然后 m10\n");
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

/* 导航任务结束时的"成绩单" —— 一行讲完这趟到底干成什么样。
 * 为什么必须自动打、而不是等人来问: 脱缆落地时串口线不在车上, 无线是 UDP 会丢, 人也不在电脑前;
 * 这一行是这趟唯一的定量记录。字段全部整数(无 printf): deg 与 mm 各自的倍率写在字段名里。 */
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
    switch (c) {
        case 'm': if (v >= 0 && v < MODE_N) { g_mode = v; g_target = 0; g_m1duty = 0; g_m2duty = 0; g_dv = 0; g_dw = 0; reset_all_pid(); } break;
        case 't': g_target = v; reset_all_pid(); break;
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
        /* u0 = 编码器计数页(默认) / u1 = 水平仪页(定轴时边挪车边看)。切页时重画静态层。 */
        case 'u': if (v >= 0 && v <= 1) { g_disp = v; g_lv_static = 0; g_disp_dirty = 1;
                      uart_dbg_puts(v ? "[lcd] page=LEVEL (u0 回计数页; 挪车让小球进绿环)\n"
                                      : "[lcd] page=COUNT\n"); }
                  break;
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
        /* l<mask>: 遥测往哪些口打印。1=有线(DAP VCOM) / 2=无线(ESP) / 3=双发(默认, 见 config.h §9)。
         * 什么时候要改: 整定用 f20(50Hz) 时双发会吃掉主循环一半时间(每字节发两遍) -> 只留一个口。
         * mask=0 被忽略(不允许把所有输出关掉 = 把自己变瞎)。 */
        case 'l': if (v > 0) { uart_dbg_set_sinks((uint32_t)v);
                      uart_dbg_puts("[uart] sinks="); uart_dbg_put_int((int)uart_dbg_get_sinks());
                      uart_dbg_puts(" (1=wired DAP, 2=wireless ESP, 3=both)\n"); }
                  break;
#endif
        case 'g': imu_dump(); return;   /* IMU 验活读数(陀螺到货后 bring-up 用) */
        case '?': break;
        default:  break;
    }
    if (g_mode != mode_before) g_mode_at = g_st;   /* 换了模式 -> 硬上限重新起算 */
    print_status();
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

static void poll_uart(void)
{
    uint8_t ch;
#if CFG_ESP_UART_EN
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch))
        if (!vision_grab(ch)) feed_cmd_stream(ch, cbuf, &clen, 1);
    while (DL_UART_receiveDataCheck(ESP_UART_INST, &ch))
        if (!vision_grab(ch)) feed_cmd_stream(ch, ebuf, &elen, 1);
#else
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch)) {
        if (vision_grab(ch)) continue;
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
void SysTick_Handler(void) { encoder_poll(); g_st++; }

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
    GC9A01_Backlight(1);
    GC9A01_Init();
    motor_init();
    magnet_init();                       /* 电磁铁: 占空先归 0 再启动定时器(上电绝不许默认吸合) */
    servo_init();                        /* 转向舵机: 同理先写 0(不出脉冲=limp), 中位未标定前不许输出 */
    encoder_init();
    SysTick_Config(CPUCLK_HZ / ST_HZ);   /* 控制主时基中断(频率见 config.h ST_HZ; 见 SysTick_Handler) */
    int imu_id = imu_init();   /* ICM42688 初始化(2026-07-27 真机验活: WHOAMI=0x47/|a|=0.995g)。读不到则不阻塞主程序 */
    g_imu_ok = (imu_id == ICM42688_WHOAMI_VAL);
    /* dt 传 CFG_IMU_MS 只是初值, 每拍会用 SysTick 真实经过时间覆盖 g_att.dt */
    attitude_init(&g_att, (float)CFG_IMU_MS / 1000.0f, CFG_ATT_ALPHA);
    nav_init(&g_nav);          /* 车级导航层: 参数取 config.h §7.5, 含(现为 0 的)里程/转角标定值 */
    uf_init(&g_uf);            /* 视觉帧解析器 */
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
    uart_dbg_puts("[ctl] SAFETY: 运动超时自停 = 静默(按模式, h<ms>可临时改) + 硬上限 ");
    uart_dbg_put_int(CFG_RUN_MS_HARDCAP);
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
    uart_dbg_puts("[ctl] enc=4x quad ENC_CPR=800(cal) | build "); uart_dbg_puts(__DATE__); uart_dbg_puts(" "); uart_dbg_puts(__TIME__); uart_dbg_puts("\n");
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

        /* 进位置模式: 捕获当前计数为零点 -> 目标是"相对入模点位移"(入模=保持当前位, 不驱回boot零点; 大计数也安全) */
        if (g_mode == MODE_POSITION && prev_mode != MODE_POSITION) { pos_ref[0] = c0; pos_ref[1] = c1; }
        prev_mode = g_mode;

        /* ---- 运动安全: 两道闸门(语义/取舍见 config.h §7) ----
         * 放在 switch(g_mode) 之前 ⇒ 触发的同一拍里 MODE_IDLE 分支就把 PWM 打成 0, 不多跑一拍。 */
        if (g_mode != MODE_IDLE) {
            uint32_t lim   = run_limit_ms(g_mode);
            uint32_t quiet = (uint32_t)(now - g_cmd_at);    /* 静默了多久 */
            uint32_t held  = (uint32_t)(now - g_mode_at);   /* 在本模式待了多久 */
            const char *why = 0; uint32_t age = 0;
            if (held >= (uint32_t)CFG_RUN_MS_HARDCAP * ST_PER_MS)  { why = "HARDCAP"; age = held;  }
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
        if ((uint32_t)(now - last_dsp) >= DISP_MS * ST_PER_MS) {
            last_dsp = now;
            /* 水平仪页只在 IDLE 允许(它比计数页重得多) —— 一进运动模式自动切回,
             * 免得"为了看屏"把控制环的时基又拖歪(本工程为此吃过 RPM 虚高 5x 的亏)。 */
            if (g_disp == 1 && g_mode != MODE_IDLE) {
                g_disp = 0; g_disp_dirty = 1;
                uart_dbg_puts("[lcd] 进运动模式 -> 自动切回计数页(水平仪页仅 IDLE 可用)\n");
            }
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
