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

/* ---- DRV8231 电流采样 (ADC0 序列: MEM0=PA27 M1, MEM1=PA26 M2) ---- */
void motor_read_current_raw(uint16_t *m1_raw, uint16_t *m2_raw)
{
    DL_ADC12_enableConversions(ADC_CUR_INST);
    DL_ADC12_startConversion(ADC_CUR_INST);
    delay_cycles(20000);   /* ~0.6ms @32MHz, 足够 2 通道序列转换完成 */
    *m1_raw = DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_0);
    *m2_raw = DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_1);
}

int32_t motor_current_ma(uint16_t raw)
{
    /* V=raw/4096*3300mV; I(mA)=V/(1.575mA/A*0.680kΩ? )  -> raw*3300/(4096*1.071) */
    return (int32_t)((uint32_t)raw * 3300u / 4387u);
}
