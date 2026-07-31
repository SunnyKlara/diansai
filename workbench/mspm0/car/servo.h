#ifndef SERVO_H
#define SERVO_H
/*
 * servo.h - 转向舵机层（阿克曼底盘前轮转向）
 *
 * 硬件事实（真值源 = 载板 §10.1，2026-07-27 定版）:
 *   信号 = PA31 = TIMG12_C1，50Hz（周期 20ms）
 *   舵机电源 **从 5V 轨取 + 共地**，⚠ 不要接 MCU 的 3V3（堵转峰值上安，会把 MCU 拖复位）
 *
 * ── 为什么舵机必须独占一个定时器 ──
 *   TIMA1 明明还空着 C1 通道，但**同一个定时器只有一个计数器 ⇒ 只有一个周期**：
 *   电磁铁要 20kHz、舵机要 50Hz，不可兼得。所以只能另开 TIMG12。
 *   （TIMA0=双电机 / TIMA1=电磁铁 / TIMG8=编码器 QEI 之外，TIMG12 是唯一空闲的。）
 *
 * ── CC 值与脉宽的关系（这条别改，来源是真机验证过的 motor.c）──
 *   本工程约定 **CC 计数 = 高电平时长**，CC=0 ⇒ 不出脉冲。
 *   证据链：① SysConfig 为 dutyCycle=0 生成的是 `CC = period`，看着像反的；
 *           ② 但生成的 PWMConfig 里 `startTimer = DL_TIMER_STOP` —— 计数器不由 init 启动，
 *              而 motor.c / magnet.c 都是**先覆写 CC 再 startCounter**，
 *              所以 SysConfig 那个初值从来没生效过，不必去解释它；
 *           ③ motor.c 写 `CC = pct*PERIOD/100`，CC=0 就是停车，这条被速度环/位置环
 *              在真机上反复验证过。舵机沿用同一约定（同 PWM 模式、同 OCTL 设置）。
 *   ⇒ 脉宽换算：CC = us * SERVO_TICKS_PER_US（32MHz ⇒ 32 counts/us，分辨率 31.25ns）。
 *   ⚠ 尽管如此，**PA31 上真实脉宽仍 `待真机验证`**（从没用示波器/舵机验过这个引脚）。
 *     bring-up 第一步在两种假设下都安全，见文末。
 *
 * ── 为什么"中位"是运行时命令而不是只在 config.h ──
 *   找中位要试很多次，而每次改 config.h 重烧要 112s 且触发本工程禁忌
 *   （SSOT §D2 第 2 条："连续快烧"曾把本板怼进 lockup）。
 *   所以给了 `C<us>` 在线设中位，**定完必须回填 config.h 并 commit**，否则断电即失。
 *   这条与 IMU 定轴命令 `a`/`s` 是同一个已被实证的理由。
 *
 * ── 两级限幅（缺一不可）──
 *   ① 角度级：|deg| <= max_deg —— 阿克曼几何/转向拉杆能达到的最大转角；
 *   ② 脉宽级：us 夹在 [min_us, max_us] —— **硬保护**，防止把拉杆顶到机械限位后舵机堵转烧毁。
 *   两级都要，因为标定错时①算出来的可能已经越界，②是最后一道墙。
 *
 * 状态: 2026-07-27 新建。🔁 **2026-07-31 口径改正：这一路现在推的是 H 题的滚球摆杆，不是阿克曼
 *       前轮**（底盘已改回差速，转向线封存 tag `servo-ackermann-v1`）。下面所有 "前轮转向角" 一律
 *       读作 **摆杆角**；两者差一个传动比、量级差约 8~9 倍。实装舵机 = **MG995**（非 LD-1501MG）。
 *       ✅ **脉宽链路已真机验证**（⛔ 旧记的"PA31 从未输出过脉冲"作废）：示波器 1500/1600/1700/1800
 *       四档核对命令 us == 引脚 us、`CFG_SERVO_CC_INVERT=1` 定版、`U1500` 舵盘走中位有保持力矩。
 *       仍 ⬜ 的只是**装到摆杆上之后的标定值**（中位/每度微秒数，见 config.h §7.9 的 ⬜ 标记）。
 *
 * ── 真机 bring-up 顺序（在两种 CC 极性假设下都不会撞机械限位）──
 *   0. **先把摆杆推杆/拉杆从舵机盘上摘掉**（中位与符号都还没定，装着连杆发命令可能直接顶限位堵转）。
 *   1. 发 `U1500` —— 若 CC 约定如上，舵机走到中位并保持；若约定是反的，舵机看到的是
 *      18.5ms 高电平（非法帧），绝大多数舵机的反应是"不动/保持"，不会满舵。两种情况都安全。
 *   2. `U1400` / `U1600` 各发一次，看舵盘是否朝相反方向小幅转动 ⇒ 证明脉宽映射有效 + 定 SIGN。
 *   3. 装回推杆。找中位有两条路，**任选其一**：
 *        a) `tools/ball_ident.ps1 -Step Sweep` —— 扫几个 us 看球的加速度，直线零点即中位（推荐：
 *           它顺带给出 K_total / 符号 / 死区，且不依赖人眼判断"平不平"）；
 *        b) `U` 小步扫到**球放中点松手 10s 不漂**，再发 `C0` 把当前脉宽锁成中位。
 *      ⚠️ 判据是"球不漂"，**不是量角器也不是水平仪**（审题补 4.4）——那才是球方程里 θ=0 的定义。
 *   4. 扫到摆杆机械限位前一点点，记下 us → 回填 config.h 的 MIN/MAX_US 与 MAX_DEG。
 *      ⚠️ 摆杆的机械上限由题目给死：h≥5cm + 杆长 25cm ⇒ 约 11.54°（`BALL_THETA_MECH_MAX_DEG`），
 *      而控制上只用到 ±6°（`CFG_BALL_THETA_MAX`）⇒ 别把限幅开到舵机的物理极限。
 *   5. 全部回填后重烧一次，`?` 确认 cal=YES。
 */
#include <stdint.h>
#include "config.h"

/* 舵机标定参数。做成结构体而不是直接读宏, 是为了 pc_test 能用多组标定值测边界。 */
typedef struct {
    int   center_us;    /* 中位脉宽 us = **摆杆水平**时的脉宽（判据"球放中点不漂", 见文件头第 3 步）*/
    int   min_us;       /* 脉宽硬下限 us（机械限位内侧）*/
    int   max_us;       /* 脉宽硬上限 us */
    float us_per_deg;   /* 每 **摆杆角** 一度对应多少 us（含摇臂+拉杆传动比, 约 8.34:1, 不是舵机自己的 us/度）*/
    float max_deg;      /* 单侧最大 **摆杆角**（机械行程决定; 题目侧上限约 11.54°, 控制只用 ±6°）*/
    int   sign;         /* +1: 脉宽增大 = 摆杆往 +θ 抬; -1: 反之。⚠ 与 CFG_BALL_SERVO_SIGN 是两件事 */
} servo_cal_t;

/* ---------------- 以下为纯逻辑（不依赖 HAL），pc_test/test_servo.c 直接测 ---------------- */

static inline void servo_cal_default(servo_cal_t *c)
{
    c->center_us  = CFG_SERVO_CENTER_US;
    c->min_us     = CFG_SERVO_MIN_US;
    c->max_us     = CFG_SERVO_MAX_US;
    c->us_per_deg = CFG_SERVO_US_PER_DEG;
    c->max_deg    = CFG_SERVO_MAX_DEG;
    c->sign       = CFG_SERVO_SIGN;
}

/* 标定是否自洽。不自洽时上层应拒绝按角度控制(但仍允许 U<us> 原始脉宽, 否则没法标定)。
 * 判的是"数值有没有互相矛盾", **不代表已经真机标过** —— 后者看 CFG_SERVO_CALIBRATED。 */
static inline int servo_cal_sane(const servo_cal_t *c)
{
    if (c->min_us <= 0 || c->max_us <= c->min_us)        return 0;
    if (c->center_us < c->min_us || c->center_us > c->max_us) return 0;
    if (!(c->us_per_deg > 0.0f))                          return 0;
    if (!(c->max_deg    > 0.0f))                          return 0;
    if (c->sign != 1 && c->sign != -1)                    return 0;
    return 1;
}

/* 脉宽硬限幅。传 0 表示"不出脉冲(limp)", 原样放过 —— 0 不是一个要被夹到 min_us 的角度值。 */
static inline int servo_us_clamp(const servo_cal_t *c, int us)
{
    if (us == 0) return 0;
    if (us < c->min_us) return c->min_us;
    if (us > c->max_us) return c->max_us;
    return us;
}

/* 角度 -> 脉宽。两级限幅: 先夹角度(几何), 再夹脉宽(硬保护)。 */
static inline int servo_us_from_deg(const servo_cal_t *c, float deg)
{
    if (deg >  c->max_deg) deg =  c->max_deg;
    if (deg < -c->max_deg) deg = -c->max_deg;
    float us = (float)c->center_us + (float)c->sign * deg * c->us_per_deg;
    int   i  = (int)(us + (us >= 0.0f ? 0.5f : -0.5f));   /* 四舍五入, 不用 math.h */
    return servo_us_clamp(c, i);
}

/* 脉宽 -> 角度（回读/遥测用）。us=0(limp) 时按中位报 0。 */
static inline float servo_deg_from_us(const servo_cal_t *c, int us)
{
    if (us == 0) return 0.0f;
    return (float)(us - c->center_us) / (c->us_per_deg * (float)c->sign);
}

/* 脉宽 -> 定时器 CC 计数（= 高电平时长, 见文件头证据链）。 */
static inline uint32_t servo_ticks_from_us(int us)
{
    if (us <= 0) return 0u;
    uint32_t t = (uint32_t)us * (uint32_t)SERVO_TICKS_PER_US;
    if (t > (uint32_t)SERVO_PWM_PERIOD) t = (uint32_t)SERVO_PWM_PERIOD;
    return t;
}

/* ---------------- 以下需要 HAL（实现在 servo.c）---------------- */

/* 初始化: 标定取 config.h 默认值, 占空先写 0(不出脉冲=limp) 再启动计数器。
 * 顺序不能反, 理由同 motor_init()/magnet_init(): 中位尚未真机标定, 上电就输出"猜的中位"
 * 可能把转向拉杆顶到机械限位并让舵机长期堵转(舵机就是这么烧的)。 */
void servo_init(void);

/* 直接给原始脉宽 us（标定用）。0 = 停脉冲(limp)。返回实际写入值(已限幅)。 */
int  servo_write_us(int us);

/* 按转向角度给（正=左 或按 CFG_SERVO_SIGN）。返回实际写入的脉宽 us。 */
int  servo_set_deg(float deg);

/* 停脉冲, 舵机失去保持力矩。 */
void servo_off(void);

/* 在线设中位: us>0 直接设; us==0 表示"把当前脉宽认作中位"(扫到正前方后一键锁定)。
 * 返回 1 成功 / 0 被拒(会破坏标定自洽性, 例如当前是 limp 或超出硬限幅)。
 * ⚠ 只活在 RAM 里, 断电即失 —— 定完必须回填 config.h。 */
int  servo_set_center_us(int us);

int   servo_us(void);            /* 当前脉宽 us（0=limp）*/
float servo_deg(void);           /* 当前角度（由脉宽反算）*/
const servo_cal_t *servo_cal(void);

/* CC 值与高电平时长的极性（0=直接 CC=高电平ticks / 1=反相 CC=周期-ticks）。
 * 2026-07-27 真机(示波器)发现 PA31 恒高、写 CC=0 反而电平更高 ⇒ 极性存疑，
 * 故做成运行时可切（命令 `Y0`/`Y1`），一次烧录试完两种，避免"连续快烧"禁忌。
 * **定下来必须回填 `CFG_SERVO_CC_INVERT` 并 commit**，否则断电即失。 */
int  servo_cc_invert(void);
void servo_set_cc_invert(int inv);

#endif /* SERVO_H */
