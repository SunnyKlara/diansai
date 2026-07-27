/*
 * magnet.c - 电磁铁通道驱动实现（板载第三颗 DRV8231, TIMA1_C0 = PB0 单向）
 * 设计取舍、单向的后果、以及"电流判不出吸住了没"的理由全在 magnet.h, 这里只写实现。
 * 状态: 2026-07-27 新建。**编译级; 真机零验证**。
 */
#include "ti_msp_dl_config.h"
#include "magnet.h"
#include "motor.h"      /* motor_adc_read_all(): ADC 只有一个, 触发点必须只有一处 */
#include "uart_dbg.h"
#include "config.h"

static magnet_state_t g_state = MAG_OFF;
static int      g_duty   = 0;
static uint32_t g_on_at  = 0;   /* 进入通电的时刻(ms) */
static int      g_manual = 0;   /* magnet_set() 手动占空: 不再自动降额 */

static void mag_pwm(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > CFG_MAG_PWM_CAP) pct = CFG_MAG_PWM_CAP;   /* [保护] 12V 直供、无电流环, 封顶防过热 */
    g_duty = pct;
    DL_TimerA_setCaptureCompareValue(PWM_MAG_INST,
        (uint32_t)pct * MOTOR_PWM_PERIOD / 100u, DL_TIMER_CC_0_INDEX);
}

void magnet_init(void)
{
    /* 先把占空写 0 再启动计数器 —— 顺序不能反: 电磁铁一通电就在发热, 上电瞬间吸一下
     * 既费电又会把还没摆好的铁件吸过来。与 motor_init() 同一条原则。 */
    mag_pwm(0);
    DL_TimerA_startCounter(PWM_MAG_INST);
    g_state = MAG_OFF; g_on_at = 0; g_manual = 0;
}

void magnet_on(void)
{
    g_state  = MAG_PULL;
    g_manual = 0;
    g_on_at  = 0;                  /* 由 magnet_tick 首拍补上真实时刻(这里拿不到 now_ms) */
    mag_pwm(100);                  /* 满占空吸合(会被 CFG_MAG_PWM_CAP 限幅) */
}

void magnet_off(void)
{
    g_state  = MAG_OFF;
    g_manual = 0;
    g_on_at  = 0;
    mag_pwm(0);
}

void magnet_set(int pct)
{
    if (pct <= 0) { magnet_off(); return; }
    g_manual = 1;
    g_state  = MAG_HOLD;           /* 语义上等于"就按这个占空保持", 不再自动降额 */
    if (g_on_at == 0) g_on_at = 1; /* 非 0 即"在通电", 真实时刻由 tick 修正 */
    mag_pwm(pct);
}

void magnet_tick(uint32_t now_ms)
{
    if (g_state == MAG_OFF) { g_on_at = 0; return; }
    if (g_on_at == 0 || g_on_at == 1) g_on_at = now_ms;   /* 首拍锚定真实时刻 */

    uint32_t on = now_ms - g_on_at;

    /* ① 吸合 -> 保持: 气隙闭合后维持吸力所需电流小得多, 继续满占空只是在发热 */
    if (g_state == MAG_PULL && !g_manual && on >= (uint32_t)CFG_MAG_PULL_MS) {
        g_state = MAG_HOLD;
        mag_pwm(CFG_MAG_HOLD_PCT);
    }

    /* ② 通电总时长上限: 与运动超时自停同一个理由 —— 一次"忘发 E0"或脚本崩掉就是线圈一直发热,
     *    而电磁铁不动不响, **没有任何现象会提醒你**。所以必须由固件兜。 */
    if (CFG_MAG_MAX_ON_MS > 0 && on >= (uint32_t)CFG_MAG_MAX_ON_MS) {
        magnet_off();
        uart_dbg_puts("\n[mag] !! MAGNET TIMEOUT after ");
        uart_dbg_put_int((int)on);
        uart_dbg_puts("ms -> OFF (线圈通电上限 CFG_MAG_MAX_ON_MS, 防过热)\n");
    }
}

magnet_state_t magnet_state(void) { return g_state; }
int            magnet_duty(void)  { return g_duty; }

uint32_t magnet_on_ms(uint32_t now_ms)
{
    if (g_state == MAG_OFF || g_on_at == 0 || g_on_at == 1) return 0;
    return now_ms - g_on_at;
}

int32_t magnet_current_ma(void)
{
    uint16_t raw = 0;
    motor_adc_read_all(0, 0, &raw);
    /* 定标沿用电机那套(同型号 DRV8231 + 同 IPROPI 电阻的假设)。
     * ⚠ 若载板给电磁铁那颗配的 IPROPI 电阻与电机不同, 这个换算就是错的 —— 需要串万用表实测一点校准。
     *   `待实测`。 */
    return motor_current_ma(raw);
}

int magnet_coil_ok(int32_t ma)
{
    /* 只判"通电了没": 吸合中电流应该明显大于噪声底(±40~100mA, 见 config.h CUR_AVG_N)。
     * ⚠ 再说一次: 这条**不是**"吸住了没"的判据(见 magnet.h 文件头)。 */
    return (ma >= CFG_MAG_MIN_MA) ? 1 : 0;
}
