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

/* ★所有可调参数(时基/周期/PWM上限/ENC_CPR/PID增益/死区/容差/调试开关)已集中到 config.h。
 * 本文件只写逻辑, 不再散落 #define —— 赛场调参只翻 config.h(工作台规范 §2 铁律)。
 * 参数的出处/实测依据写在 config.h 每个宏旁边; 事实真值源见 .kiro/steering/工程事实SSOT.md。 */
#include "config.h"

static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

/* ==== 模式 ==== */
enum { MODE_IDLE = 0, MODE_CURRENT, MODE_SPEED, MODE_POSITION, MODE_OPEN, MODE_DUAL,
       MODE_DRIVE, MODE_DRIVE_CL, MODE_N };
static const char *mode_name[MODE_N] = { "IDLE", "CURR", "SPD", "POS", "OPEN", "DUAL", "DRV", "DRVC" };

/* ==== 运行时(串口可改) ==== */
static volatile int g_mode   = MODE_IDLE;
static volatile int g_target = 0;       /* mA / RPM / counts / PWM% (随模式) */
static volatile int g_print_ms = PRINT_MS;   /* 遥测周期(真实ms). 整定时 f20 调快抓暂态, f100 复原 */
/* 位置环精定位(2026-07-25, 运行时可调免重烧): 死区前馈% + 到位死区counts */
static int g_pwm_dz  = CFG_POS_FF_DZ;   /* 死区前馈%(默认值+依据见 config.h). 运行时命令 w<v> 可改 */
static int g_pos_tol = CFG_POS_TOL;     /* 到位死区counts(默认值+依据见 config.h). 运行时命令 e<v> 可改 */
static int g_m1duty  = 0, g_m2duty = 0;  /* MODE_DUAL 调试(m5): 独立每电机 PWM%(命令 x/y), 每拍读双电流, 专门测通道串扰 */
/* 差速层(m6 DRIVE 开环 / m7 DRIVE_CL 闭环)的车级指令: 命令 v<线速度> / r<角速度>。
 * m6 单位=PWM%(开环, 用来验差速层与左右分组是否正确, 离地即可验);
 * m7 单位=RPM(左右目标各喂一个已达标的速度环 —— 两轮机械不匹配, 同 PWM 走不直, 必须双轮独立闭环)。 */
static int g_dv = 0, g_dw = 0;
/* ==== IMU / 航向状态（详见文件后半 "姿态/航向" 段的职责分工说明） ==== */
static attitude_t g_att;                 /* 姿态状态(yaw/pitch/roll + 陀螺零偏) */
static int   g_imu_ok   = 0;             /* imu_init 是否读到 0x47 */
static int   g_cal_left = 0;             /* >0 = 零偏标定进行中, 还差几个样本(命令 k 启动) */
static float g_wz_dps   = 0.0f;          /* 去零偏(+死区)后的偏航角速度(dps), 遥测/将来当航向环 D 项 */
/* 定轴两参数做成运行时可改(命令 a<0|1|2> / s<±1>): 定轴要试多种组合, 若靠重烧就会触发
 * "连续快烧"这个已把芯片怼进 lockup 的禁忌(SSOT §D2)。默认值来自 config.h, 定完回填锁死。 */
static int   g_yaw_axis = CFG_YAW_AXIS;
static int   g_yaw_sign = CFG_YAW_SIGN;
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
    reset_all_pid();
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
        default:            return 0;
    }
}
static int loop_index(void)   /* 当前模式对应的增益索引; 无环返回 -1 */
{
    if (g_mode == MODE_CURRENT)  return 0;
    if (g_mode == MODE_SPEED)    return 1;
    if (g_mode == MODE_DRIVE_CL) return 1;   /* 闭环差速走的就是速度环 -> p/i/d 直接调它 */
    if (g_mode == MODE_POSITION) return 2;
    return -1;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ==== 串口命令 ==== */
static char cbuf[16];
static int  clen = 0;
static uint32_t g_cmd_rej = 0;      /* 被格式门拒掉的行数(遥测里可见=门在干活) */

/* 命令健壮化(2026-07-27 加, 为"ESP-01S 无线遥测"铺路) —— 两层防护:
 *
 * 【层1: 上电静默窗 CMD_MUTE_MS】上电后头一段时间只读走 RX 字节、不解析。
 * 【层2: 严格格式门 cmd_format_ok】只接受 "<字母>[-][数字...]" 这一种形状(允许可选 '#' 前缀)。
 *
 * 起因(PC 侧回放实测, 见 esp_boot_risk.ps1 与证据
 *      ../../天猛星主板平台/无线遥测_ESP01S_boot字节证据.txt):
 *   把 ESP-01S 的 TXD 接到 MCU 的 UART RX 后, ESP 每次上电/掉压复位都会吐一段 boot 日志,
 *   里面的 "tail 0" / "tail 4" / "tail 0" 三行, 首字符正好是本固件的命令 't'(设定目标值)!
 *   parse_int(" 0") 遇空格即返回 0 => 每次 ESP 上电都会静默执行 3 次 "改目标 + reset_all_pid()",
 *   真车上表现为"跑着突然停/目标莫名归零", 而凶手在 ESP 的启动日志里, 查电机/编码器/PID 永远查不到。
 *   同段日志还含纯乱码行, 撞上 'x'/'y' 就是直接驱动电机。
 *
 * 为什么用"格式门"而不是"加 '#' 帧头":
 *   现有全部上位机脚本(uart_send / tune_step / pos_step / disturb_test / iloop / xtalk)发的都是
 *   严格的 "<字母><数字>" (m3 / t900 / p150 / z / ?), 所以格式门**向后 100% 兼容、脚本零改动**;
 *   而实测那段 boot 日志逐行都被它拒掉(每行都含空格或多余字母)。'#' 前缀仍被接受, 想再加固可随时切。
 */
#define CMD_MUTE_MS   2000UL        /* 上电后静默期(ms): 覆盖 ESP boot 日志全程(实测 ~600ms 内结束) */

static int cmd_format_ok(const char *s, int n)
{
    int i = 1;                                  /* s[0]=命令字母, 由 run_cmd 的 switch 认 */
    if (n < 1) return 0;
    if (i < n && s[i] == '-') i++;               /* 允许负数(如 x-300) */
    for (; i < n; i++) { if (s[i] < '0' || s[i] > '9') return 0; }
    return 1;
}

static int parse_int(const char *s, int n)
{
    int i = 0, sg = 1, v = 0;
    if (i < n && s[i] == '-') { sg = -1; i++; }
    for (; i < n; i++) { if (s[i] < '0' || s[i] > '9') break; v = v * 10 + (s[i] - '0'); }
    return sg * v;
}
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
    /* 被格式门拒掉的行数: 接了 ESP 之后每次 ESP 上电应看到它跳 ~20(=boot 日志行数), 是"门在干活"的证据 */
    uart_dbg_puts(" rej="); uart_dbg_put_int((int)g_cmd_rej); uart_dbg_puts("\n");
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
        case 'p': li = loop_index(); if (li >= 0) { gkp[li] = v / 1000.0f; apply_gains(); } break;
        case 'i': li = loop_index(); if (li >= 0) { gki[li] = v / 1000.0f; apply_gains(); } break;
        case 'd': li = loop_index(); if (li >= 0) { gkd[li] = v / 1000.0f; apply_gains(); } break;
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
        case 'g': imu_dump(); return;   /* IMU 验活读数(陀螺到货后 bring-up 用) */
        case '?': break;
        default:  break;
    }
    if (g_mode != mode_before) g_mode_at = g_st;   /* 换了模式 -> 硬上限重新起算 */
    print_status();
}
static void poll_uart(void)
{
    uint8_t ch;
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch)) {
        /* 层1: 上电静默窗——字节必须读走(否则 FIFO 里的旧字节会在解禁后被当命令解析), 但不入 buffer */
        if (g_st < CMD_MUTE_MS * ST_PER_MS) { clen = 0; continue; }

        if (ch == '\r' || ch == '\n') {
            if (clen > 0) {
                char *p = cbuf;
                int   n = clen;
                if (p[0] == '#') { p++; n--; }       /* 可选帧头, 想加固时上位机加 '#' 即可 */
                /* 层2: 严格格式门——形状不对的整行丢弃(ESP boot 日志/乱码全落这里) */
                if (n > 0 && cmd_format_ok(p, n)) run_cmd(p, n);
                else g_cmd_rej++;
                clen = 0;
            }
        }
        else if (clen < 15) cbuf[clen++] = (char)ch;
    }
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
    encoder_init();
    SysTick_Config(CPUCLK_HZ / ST_HZ);   /* 控制主时基中断(频率见 config.h ST_HZ; 见 SysTick_Handler) */
    int imu_id = imu_init();   /* ICM42688 初始化(2026-07-27 真机验活: WHOAMI=0x47/|a|=0.995g)。读不到则不阻塞主程序 */
    g_imu_ok = (imu_id == ICM42688_WHOAMI_VAL);
    /* dt 传 CFG_IMU_MS 只是初值, 每拍会用 SysTick 真实经过时间覆盖 g_att.dt */
    attitude_init(&g_att, (float)CFG_IMU_MS / 1000.0f, CFG_ATT_ALPHA);

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
    uart_dbg_puts("[ctl] cmds: t<v> tgt | v<lin> r<ang> drive | p/i/d<x1000> gains | w<%>dz e<cnt>tol(pos精定位) | f<ms> | z stop | ?\n");
    uart_dbg_puts("[ctl] IMU: g dump | k bias-cal(静止2s) | o yaw=0 | a<0|1|2>定轴 s<1|-1>定符号 ; telemetry Y=yaw(0.1deg) W=wz(0.01dps)\n");
    uart_dbg_puts("[ctl] SAFETY: 运动超时自停 = 静默(按模式, h<ms>可临时改) + 硬上限 ");
    uart_dbg_put_int(CFG_RUN_MS_HARDCAP);
    uart_dbg_puts("ms(不可绕过); 触发会打 'RUN TIMEOUT'\n");
    uart_dbg_puts("[ctl] enc=4x quad ENC_CPR=800(cal) | build "); uart_dbg_puts(__DATE__); uart_dbg_puts(" "); uart_dbg_puts(__TIME__); uart_dbg_puts("\n");
    uart_dbg_puts("[imu] init WHOAMI="); uart_dbg_put_int(imu_id);
    uart_dbg_puts(imu_id == ICM42688_WHOAMI_VAL ? " OK(ICM42688)\n" : " 未就绪(接线/供电/片选异常, 用 g 命令复测)\n");
    uart_dbg_puts("[imu] yaw axis="); uart_dbg_put_int(g_yaw_axis);
    uart_dbg_puts(" sign=");          uart_dbg_put_int(g_yaw_sign);
    uart_dbg_puts(" (来自 config.h, // 待真机定轴; 上电后未 k 标定前 yaw 会漂)\n");
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
        poll_uart();
        uint32_t now = g_st;   /* 真实时基快照(SysTick 5kHz); 编码器采样在同一 ISR */

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
        if ((uint32_t)(now - last_spd) >= SPEED_MS * ST_PER_MS) {
            float dt_ms = (float)(uint32_t)(now - last_spd) / (float)ST_PER_MS;   /* 真实经过 ms(修正5x scaling) */
            float k = 60000.0f / (ENC_CPR * dt_ms);                               /* counts/窗 -> RPM(真实dt) */
            speed_rpm[0] = (float)(c0 - lastc[0]) * k;
            speed_rpm[1] = (float)(c1 - lastc[1]) * k;
            lastc[0] = c0; lastc[1] = c1;
            last_spd = now;
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
            if (g_cal_left > 0) {                 /* 零偏标定中: 只采样, 不积分(车必须静止) */
                attitude_bias_sample(&g_att, gm);
                if (--g_cal_left == 0) {
                    attitude_bias_apply(&g_att);
                    attitude_reset_yaw(&g_att, 0.0f);
                    uart_dbg_puts("[imu] bias done 0.01dps: ");
                    uart_dbg_put_int((int)(g_att.gbias[0]*100)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_att.gbias[1]*100)); uart_dbg_putc(',');
                    uart_dbg_put_int((int)(g_att.gbias[2]*100));
                    uart_dbg_puts("  yaw reset to 0\n");
                }
            } else {
                /* 死区作用在"去零偏后"的角速度上。attitude_update 内部会再减一次 gbias,
                 * 所以这里把死区结果加回 gbias 传进去 —— 等效于"先去偏->死区->再积分",
                 * 且 attitude.c(PC 单测过的算法)一行不用改。 */
                float wz = gm[2] - g_att.gbias[2];
                if (wz < CFG_GYRO_DEADBAND_DPS && wz > -CFG_GYRO_DEADBAND_DPS) wz = 0.0f;
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

        case MODE_DRIVE_CL: {           /* m7: 闭环差速. v/r 单位=RPM, 左右各喂一个已达标的速度环(走直线用这个) */
            int l, r;
            car_drive_mix(g_dv, g_dw, &l, &r);
            if (spd_tick) {
                /* 目标 0 时强制停 + 清积分: 否则停车指令下积分残留会让轮子 creep(参考工程同做法)。 */
                if (l == 0) { pid_reset(&pid_v[0]); pwm_out[0] = 0; }
                else        { pwm_out[0] = (int)pid_step(&pid_v[0], (float)l, speed_rpm[0]); }
                if (r == 0) { pid_reset(&pid_v[1]); pwm_out[1] = 0; }
                else        { pwm_out[1] = (int)pid_step(&pid_v[1], (float)r, speed_rpm[1]); }
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
            /* 航向: Y=yaw(0.1°, 连续累计可超±360, 便于"转5圈"标度校验) W=偏航角速度(0.01dps) */
            uart_dbg_puts(" | Y:"); uart_dbg_put_int((int)(g_att.yaw * 10.0f));
            uart_dbg_puts(" W:");   uart_dbg_put_int((int)(g_wz_dps * 100.0f));
            if (g_cal_left > 0) uart_dbg_puts(" CAL");
            uart_dbg_puts("\n");
        }

        /* LCD 计数刷新: 仅在计数变化时重绘对应行(静止零刷=不闪烁),
         * 固定宽度右对齐 + 不透明黑底 => 覆盖式绘制, 免 FillRect 清屏、无残影、位置不漂。 */
        if ((uint32_t)(now - last_dsp) >= DISP_MS * ST_PER_MS) {
            last_dsp = now;
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
        }

        delay_ms(1);   /* 轻延时防空转; 控制/遥测/LCD 均按 g_st 真实时间调度, 不依赖此延时的精度 */
    }
}
