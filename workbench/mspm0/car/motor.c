/*
 * motor.c - DRV8231 双电机驱动实现 (MSPM0 driverlib, TIMA0 4通道PWM)
 * 引脚由 SysConfig 生成: PWM_MOTOR_INST=TIMA0, CC0/1=M1(PA8/PA9), CC2/3=M2(PB12/PB13)。
 */
#include "ti_msp_dl_config.h"
#include "motor.h"

/* 把占空比(0..PERIOD)写到某个 CC 通道 */
static inline void cc_set(uint32_t idx, uint32_t val)
{
    DL_TimerA_setCaptureCompareValue(PWM_MOTOR_INST, val, idx);
}

void motor_init(void)
{
    /* 先把 4 路都设 0(停),再启动计数器 —— 上电电机不乱动(安全) */
    cc_set(DL_TIMER_CC_0_INDEX, 0);
    cc_set(DL_TIMER_CC_1_INDEX, 0);
    cc_set(DL_TIMER_CC_2_INDEX, 0);
    cc_set(DL_TIMER_CC_3_INDEX, 0);
    DL_TimerA_startCounter(PWM_MOTOR_INST);
}

void motor_set(uint8_t ch, int16_t duty)
{
    if (duty > 100)  duty = 100;
    if (duty < -100) duty = -100;

    /* 占空幅值 -> CC 计数 */
    uint32_t mag = (uint32_t)((duty < 0) ? -duty : duty) * MOTOR_PWM_PERIOD / 100u;
    if (mag > MOTOR_PWM_PERIOD) mag = MOTOR_PWM_PERIOD;

    uint32_t in1, in2;
    if (duty >= 0) { in1 = mag; in2 = 0;   }   /* 正转: IN1=PWM, IN2=0 */
    else           { in1 = 0;   in2 = mag; }   /* 反转: IN1=0, IN2=PWM */

    if (ch == MOTOR_M1) {
        cc_set(DL_TIMER_CC_0_INDEX, in1);      /* M1 IN1 = PA8 */
        cc_set(DL_TIMER_CC_1_INDEX, in2);      /* M1 IN2 = PA9 */
    } else {
        cc_set(DL_TIMER_CC_2_INDEX, in1);      /* M2 IN1 = PB12 */
        cc_set(DL_TIMER_CC_3_INDEX, in2);      /* M2 IN2 = PB13 */
    }
}

void motor_stop_all(void)
{
    motor_set(MOTOR_M1, 0);
    motor_set(MOTOR_M2, 0);
}

/* ---- DRV8231 电流采样 (ADC0 序列: MEM0=PA27, MEM1=PA26) ----
 * ⚠ 真机实测(2026-07-24 探针自动轻转): 驱动 M1(PA8/PA9) 时电流出现在 MEM1(PA26)、
 *   驱动 M2(PB12/PB13) 时在 MEM0(PA27) —— 即 M1 的 IPROPI 实际接 PA26、M2 接 PA27,
 *   与起初 syscfg 注释假设相反。故此处按实测映射取值(m1←MEM1, m2←MEM0), 让上层"M1电流"名副其实。 */
/* 多次采样取平均: 电机 PWM 斩波使 IPROPI 电流呈脉冲(导通相高/续流相~0), 单次异步采样
 * 会随机抓在脉冲任意点 → 读数在 0~峰值间狂跳(实测 t150 时 I 抖 0~627mA)。连采 N 次
 * (跨多个 PWM 周期)取平均, 把斩波纹波平掉, 电流环才拿得到干净反馈。
 * 采样次数 CUR_AVG_N 及其取值依据见 config.h(要更快需上"ADC 触发同步 PWM 定相采样")。 */
/* 序列里的通道数, 必须与 car.syscfg 的 endAdd+1 一致(现在 MEM0/1/2 = M2电流/M1电流/电磁铁电流)。
 * ⚠ **加通道必须同时改这里**: 等待时间不够时 getMemResult 读到的是上一轮的陈旧值 ——
 *   这种错误不报警、不崩、只是数据慢半拍或干脆是别的通道的旧数, 极难从现象看出来。
 * 每通道 sampleTime0=40us(≈1280 周期 @32MHz), 按 1600 周期/通道留余量, 再加 800 周期的固定开销。 */
#define ADC_SEQ_N        3
#define ADC_WAIT_CYCLES  (ADC_SEQ_N * 1600u + 800u)

/* 一次触发, 取回整条序列的三路平均值。任一指针可为 NULL(不需要就别要)。
 * 为什么三路必须在**同一个函数**里取: 序列是一次触发全部转换, 若电机与电磁铁各自去 startConversion,
 * 两边会互相打断对方的序列 —— ADC 只有一个, 触发点也只能有一个。 */
void motor_adc_read_all(uint16_t *m1_raw, uint16_t *m2_raw, uint16_t *mag_raw)
{
    uint32_t s0 = 0, s1 = 0, s2 = 0;
    DL_ADC12_enableConversions(ADC_CUR_INST);
    for (int i = 0; i < CUR_AVG_N; i++) {
        DL_ADC12_startConversion(ADC_CUR_INST);
        delay_cycles(ADC_WAIT_CYCLES);
        s0 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_0);   /* PA27 = M2 */
        s1 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_1);   /* PA26 = M1 */
        s2 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_2);   /* PA24 = 电磁铁 */
    }
    if (m1_raw)  *m1_raw  = (uint16_t)(s1 / CUR_AVG_N);   /* M1 = MEM1(PA26, 实测) */
    if (m2_raw)  *m2_raw  = (uint16_t)(s0 / CUR_AVG_N);   /* M2 = MEM0(PA27, 实测) */
    if (mag_raw) *mag_raw = (uint16_t)(s2 / CUR_AVG_N);   /* 电磁铁 = MEM2(PA24) // 待真机验证 */
}

void motor_read_current_raw(uint16_t *m1_raw, uint16_t *m2_raw)
{
    motor_adc_read_all(m1_raw, m2_raw, 0);
}

int32_t motor_current_ma(uint16_t raw)
{
    /* V=raw/4096*3300mV; I(mA)=V/(1.575mA/A × 0.680kΩ) -> raw*3300/CUR_MA_DIV。
     * 除数 CUR_MA_DIV 及其定标依据(待实测校准)见 config.h。 */
    return (int32_t)((uint32_t)raw * 3300u / CUR_MA_DIV);
}
