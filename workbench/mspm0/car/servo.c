/*
 * servo.c - 转向舵机层实现（TIMG12_C1 = PA31, 50Hz）
 * 设计取舍、CC 值与脉宽的关系（含证据链）、两级限幅的理由、真机 bring-up 顺序全在 servo.h。
 * 这里只写 HAL 相关的那几行。
 * 状态: 2026-07-27 新建。**编译级 + PC 单测级; 真机零验证**。
 */
#include "ti_msp_dl_config.h"
#include "servo.h"
#include "config.h"

static servo_cal_t g_cal;
static int         g_us = 0;        /* 当前脉宽 us, 0 = 不出脉冲(limp) */

/* CC 值与高电平时长的极性。
 * 0 = 直接式(CC = 高电平 ticks), 沿用真机验证过的 motor.c 约定;
 * 1 = 反相式(CC = 周期 - 高电平 ticks), 即 SysConfig 为 dutyCycle=0 生成 CC=period 所暗示的语义。
 * 为什么做成运行时可切: 2026-07-27 真机(示波器)实测 PA31 恒定高、且写 CC=0 时电平反而更高,
 *   说明极性可能与 motor.c 的假设相反; 而每次改 config.h 重烧要 115s 且触发"连续快烧"禁忌
 *   (SSOT §D2 第 2 条, 本板曾因此 lockup)。⇒ 一次烧录, 用命令 `Y0`/`Y1` 试完两种。
 *   **定下来必须回填 config.h 并 commit**, 否则只活在 RAM、断电即失。 */
static int g_inv = CFG_SERVO_CC_INVERT;

static inline void srv_cc(uint32_t ticks)
{
    /* GPIO_PWM_SERVO_C1_IDX 是 SysConfig 生成的(= DL_TIMER_CC_1_INDEX)。
     * 用生成的宏而不是写死 CC_1: 万一将来把舵机挪到别的通道, 这里自动跟着变。 */
    uint32_t cc = ticks;
    if (g_inv) {
        /* 反相: 高电平时长 ticks -> CC = period - ticks。ticks=0 应给出"完全无脉冲",
         * 在反相语义下就是 CC = period(与 SysConfig 的 dutyCycle=0 初值一致)。 */
        cc = (uint32_t)SERVO_PWM_PERIOD - ticks;
    }
    DL_TimerG_setCaptureCompareValue(PWM_SERVO_INST, cc, GPIO_PWM_SERVO_C1_IDX);
}

int servo_cc_invert(void) { return g_inv; }

void servo_set_cc_invert(int inv)
{
    g_inv = inv ? 1 : 0;
    srv_cc(servo_ticks_from_us(g_us));   /* 立刻按新极性重写当前脉宽, 免得要再发一次 U */
}

void servo_init(void)
{
    servo_cal_default(&g_cal);
    g_us = 0;
    srv_cc(0);                              /* 先 0: 上电不出脉冲 = 舵机 limp, 不会顶拉杆 */
    DL_TimerG_startCounter(PWM_SERVO_INST);
}

int servo_write_us(int us)
{
    g_us = servo_us_clamp(&g_cal, us);
    srv_cc(servo_ticks_from_us(g_us));
    return g_us;
}

int servo_set_deg(float deg)
{
    return servo_write_us(servo_us_from_deg(&g_cal, deg));
}

void servo_off(void)
{
    g_us = 0;
    srv_cc(0);
}

int servo_set_center_us(int us)
{
    int want = (us == 0) ? g_us : us;       /* us==0 => 把当前脉宽认作中位 */
    if (want <= 0) return 0;                /* 当前是 limp, 没有"当前脉宽"可认 */
    if (want < g_cal.min_us || want > g_cal.max_us) return 0;

    servo_cal_t t = g_cal;
    t.center_us = want;
    if (!servo_cal_sane(&t)) return 0;
    g_cal = t;
    return 1;
}

int   servo_us(void)  { return g_us; }
float servo_deg(void) { return servo_deg_from_us(&g_cal, g_us); }

const servo_cal_t *servo_cal(void) { return &g_cal; }
