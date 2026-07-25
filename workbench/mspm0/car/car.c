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

#define CPUCLK_HZ 32000000UL
static void delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPUCLK_HZ / 1000UL); }

/* ==== 安全 / 时序 ==== */
#define PWM_CAP       60        /* PWM 上限%: 7.4V 电机 / 12V 母线, 封顶保护 */
#define BASE_TICK_MS  1         /* 基础节拍 ~1ms */
#define SPEED_DIV     50        /* 速度测量窗+速度环每 50 拍(~50ms=20Hz). 加宽窗=更多counts/窗=降量化噪声
                                 * (低速~30RPM时 20ms窗仅~10counts→±1count≈±3.3RPM噪声;50ms窗~25counts→噪声减~2.5x) */
#define POS_DIV       100       /* 位置外环每 100 拍(~100ms=10Hz, 外环慢于速度内环) */
#define PRINT_DIV     100       /* 遥测每 ~100ms */
#define DISP_DIV      25        /* LCD 计数刷新每 ~25ms(40Hz), 且仅在计数变化时才重绘 */
#define ENC_CPR       800.0f    /* 输出轴每圈计数(4x正交解码). 2026-07-25 真机手转标定: 转8圈计数增量6402 -> 800.25, 取800(原估899偏高~12%)。 */
#define SPEED_WIN_MS  (SPEED_DIV * BASE_TICK_MS)

/* ==== 编码器专项排查探针(临时) ====
 * =1 开启: main() 一进来就跑 enc_probe_run()(永不返回, 电机全程不驱动),
 *          直读 PA7/PB19/PB20/PB21 原始电平 + 软件轮询边沿计数 + ISR 计数,
 *          一屏三视图把"信号没到脚(硬件) / 中断没触发(软件) / 其实在工作"三层分开。
 * 排查完改回 0 即恢复完整控制固件。 */
#define ENC_PROBE     0

/* ==== 模式 ==== */
enum { MODE_IDLE = 0, MODE_CURRENT, MODE_SPEED, MODE_POSITION, MODE_OPEN, MODE_N };
static const char *mode_name[MODE_N] = { "IDLE", "CURR", "SPD", "POS", "OPEN" };

/* ==== 运行时(串口可改) ==== */
static volatile int g_mode   = MODE_IDLE;
static volatile int g_target = 0;       /* mA / RPM / counts / PWM% (随模式) */
static volatile int g_print_ms = PRINT_DIV;  /* 遥测周期(ms=tick数). 整定时 f20 调快抓暂态, f100 复原 */
/* 位置环精定位(2026-07-25, 运行时可调免重烧): 死区前馈% + 到位死区counts */
static int g_pwm_dz  = 12;   /* 死区前馈%(实测电机死区~10%PWM). 位置内环按【位置误差方向】叠此值->推越死区、末端不停短. 命令 w<v>. 2026-07-25真机整定=12 */
static int g_pos_tol = 15;   /* 到位死区counts(~7°@800cpr). |位置误差|<此值即停+复位PID, 防末端抖/creep. 命令 e<v>. 真机整定=15(两轮到位±6~15) */

/* 增益: 索引 0=电流环 1=速度环 2=位置环. [1]速度环+[2]位置环 2026-07-25 真机整定达标; [0]电流环待整定
 * 速度环 target=500: std~3%/超调10%/rise~710ms | 位置环(级联速度内环) 到位±~40counts(~1.5%rev),无振荡/小超调;
 * 精确定位(消死区停短的残余)后续加死区前馈或小积分,当前为粗定位可用基线 */
static float gkp[3] = { 0.20f, 0.03f, 0.20f };   /* [1]速度Kp0.03(原0.10限幅振荡) [2]位置Kp0.20(原0.02太钝) 真机达标 */
static float gki[3] = { 0.02f, 0.02f, 0.00f };   /* 位置环用纯PD、不加I(死区下积分易 hunt) */
static float gkd[3] = { 0.00f, 0.00f, 0.05f };   /* [2]位置环轻阻尼 Kd0.05 */

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
static int loop_index(void)   /* 当前模式对应的增益索引; 无环返回 -1 */
{
    if (g_mode == MODE_CURRENT)  return 0;
    if (g_mode == MODE_SPEED)    return 1;
    if (g_mode == MODE_POSITION) return 2;
    return -1;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ==== 串口命令 ==== */
static char cbuf[16];
static int  clen = 0;

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
    uart_dbg_puts(" (PWM_CAP="); uart_dbg_put_int(PWM_CAP); uart_dbg_puts("%)\n");
}
static void run_cmd(const char *s, int n)
{
    char c = s[0];
    int v = parse_int(s + 1, n - 1), li;
    switch (c) {
        case 'm': if (v >= 0 && v < MODE_N) { g_mode = v; g_target = 0; reset_all_pid(); } break;
        case 't': g_target = v; reset_all_pid(); break;
        case 'p': li = loop_index(); if (li >= 0) { gkp[li] = v / 1000.0f; apply_gains(); } break;
        case 'i': li = loop_index(); if (li >= 0) { gki[li] = v / 1000.0f; apply_gains(); } break;
        case 'd': li = loop_index(); if (li >= 0) { gkd[li] = v / 1000.0f; apply_gains(); } break;
        case 'z': g_mode = MODE_IDLE; g_target = 0; reset_all_pid(); break;
        case 'f': if (v >= 5 && v <= 2000) g_print_ms = v; break;   /* 遥测周期 ms(整定抓暂态用) */
        case 'w': if (v >= 0 && v <= 30)  g_pwm_dz  = v; break;     /* 位置死区前馈% */
        case 'e': if (v >= 0 && v <= 300) g_pos_tol = v; break;     /* 位置到位死区 counts */
        case '?': break;
        default:  break;
    }
    print_status();
}
static void poll_uart(void)
{
    uint8_t ch;
    while (DL_UART_receiveDataCheck(DBG_UART_INST, &ch)) {
        if (ch == '\r' || ch == '\n') { if (clen > 0) { run_cmd(cbuf, clen); clen = 0; } }
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
void SysTick_Handler(void) { encoder_poll(); }

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
    SysTick_Config(CPUCLK_HZ / 5000);   /* 5kHz 编码器采样中断(见 SysTick_Handler) */

    GC9A01_FillScreen(LCD_BLACK);
    GC9A01_DrawStringCentered(24, "MOTOR CTL", LCD_GREEN, LCD_BLACK, 2);
    GC9A01_DrawStringCentered(60, "ENCODER CNT", LCD_GRAY, LCD_BLACK, 2);   /* 下方两大数=两编码器计数 */

    pid_init(&pid_i[0], gkp[0], gki[0], gkd[0], 0.0f, (float)PWM_CAP);
    pid_init(&pid_i[1], gkp[0], gki[0], gkd[0], 0.0f, (float)PWM_CAP);
    pid_init(&pid_v[0], gkp[1], gki[1], gkd[1], -(float)PWM_CAP, (float)PWM_CAP);
    pid_init(&pid_v[1], gkp[1], gki[1], gkd[1], -(float)PWM_CAP, (float)PWM_CAP);
    pid_init(&pid_p[0], gkp[2], gki[2], gkd[2], -300.0f, 300.0f);   /* 位置->速度目标(RPM) */
    pid_init(&pid_p[1], gkp[2], gki[2], gkd[2], -300.0f, 300.0f);

    uart_dbg_puts("\n[ctl] boot | modes: m0 IDLE / m1 CURR / m2 SPD / m3 POS / m4 OPEN\n");
    uart_dbg_puts("[ctl] cmds: t<v> tgt | p/i/d<x1000> gains | w<%>dz e<cnt>tol(pos精定位) | f<ms> | z stop | ?\n");
    uart_dbg_puts("[ctl] enc=4x quad ENC_CPR=800(cal) | build "); uart_dbg_puts(__DATE__); uart_dbg_puts(" "); uart_dbg_puts(__TIME__); uart_dbg_puts("\n");
    print_status();

    int32_t lastc[2] = { 0, 0 };
    int32_t last_disp[2] = { (int32_t)0x80000000, (int32_t)0x80000000 };  /* LCD 上次显示值, 置不可能值->强制首帧刷 */
    float   speed_rpm[2] = { 0.0f, 0.0f };
    float   v_target[2]  = { 0.0f, 0.0f };   /* 位置环产出的速度目标 */
    int     pwm_out[2]   = { 0, 0 };
    int32_t pos_ref[2]   = { 0, 0 };          /* 位置模式入模零点(相对定位) */
    int     prev_mode    = MODE_IDLE;
    uint32_t tick = 0;

    while (1) {
        poll_uart();
        /* 编码器采样已移到 SysTick 5kHz 中断(见 SysTick_Handler), 主循环不再 poll */

        int32_t c0 = encoder_count(ENC_1);
        int32_t c1 = encoder_count(ENC_2);

        /* 进位置模式: 捕获当前计数为零点 -> 目标是"相对入模点位移"(入模=保持当前位, 不驱回boot零点; 大计数也安全) */
        if (g_mode == MODE_POSITION && prev_mode != MODE_POSITION) { pos_ref[0] = c0; pos_ref[1] = c1; }
        prev_mode = g_mode;

        /* 速度测量(固定窗口) */
        if (tick % SPEED_DIV == 0) {
            float k = 60000.0f / (ENC_CPR * (float)SPEED_WIN_MS);  /* counts/窗口 -> RPM */
            speed_rpm[0] = (float)(c0 - lastc[0]) * k;
            speed_rpm[1] = (float)(c1 - lastc[1]) * k;
            lastc[0] = c0; lastc[1] = c1;
        }

        switch (g_mode) {
        case MODE_IDLE:
            pwm_out[0] = pwm_out[1] = 0;
            break;

        case MODE_OPEN:                 /* g_target = PWM%(带符号) */
            pwm_out[0] = pwm_out[1] = clampi(g_target, -PWM_CAP, PWM_CAP);
            break;

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
            if (tick % SPEED_DIV == 0) {
                pwm_out[0] = (int)pid_step(&pid_v[0], (float)g_target, speed_rpm[0]);
                pwm_out[1] = (int)pid_step(&pid_v[1], (float)g_target, speed_rpm[1]);
            }
            break;

        case MODE_POSITION: {           /* g_target = 目标位置 counts(相对入模点) */
            int32_t rel0 = c0 - pos_ref[0], rel1 = c1 - pos_ref[1];
            if (tick % POS_DIV == 0) {  /* 外环: 相对位置->速度目标 */
                v_target[0] = pid_step(&pid_p[0], (float)g_target, (float)rel0);
                v_target[1] = pid_step(&pid_p[1], (float)g_target, (float)rel1);
            }
            if (tick % SPEED_DIV == 0) {/* 内环: 速度环 + 死区前馈 + 到位死区(精定位) */
                pwm_out[0] = pos_inner_step(&pid_v[0], v_target[0], speed_rpm[0], g_target - rel0);
                pwm_out[1] = pos_inner_step(&pid_v[1], v_target[1], speed_rpm[1], g_target - rel1);
            }
            break; }
        }

        pwm_out[0] = clampi(pwm_out[0], -PWM_CAP, PWM_CAP);
        pwm_out[1] = clampi(pwm_out[1], -PWM_CAP, PWM_CAP);
        motor_set(MOTOR_M1, (int16_t)pwm_out[0]);
        motor_set(MOTOR_M2, (int16_t)pwm_out[1]);

        if (tick % (uint32_t)g_print_ms == 0) {
            uart_dbg_puts("[ctl] "); uart_dbg_puts(mode_name[g_mode]);
            uart_dbg_puts(" tgt=");  uart_dbg_put_int(g_target);
            uart_dbg_puts(" | I:"); uart_dbg_put_int(i_meas[0]); uart_dbg_putc(','); uart_dbg_put_int(i_meas[1]);
            uart_dbg_puts(" | V:"); uart_dbg_put_int((int)speed_rpm[0]); uart_dbg_putc(','); uart_dbg_put_int((int)speed_rpm[1]);
            uart_dbg_puts(" | PWM:"); uart_dbg_put_int(pwm_out[0]); uart_dbg_putc(','); uart_dbg_put_int(pwm_out[1]);
            uart_dbg_puts(" | C:"); uart_dbg_put_int(c0); uart_dbg_putc(','); uart_dbg_put_int(c1);
            uart_dbg_puts("\n");
        }

        /* LCD 计数刷新: 仅在计数变化时重绘对应行(静止零刷=不闪烁),
         * 固定宽度右对齐 + 不透明黑底 => 覆盖式绘制, 免 FillRect 清屏、无残影、位置不漂。 */
        if (tick % DISP_DIV == 0) {
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

        tick++;
        delay_ms(BASE_TICK_MS);
    }
}
