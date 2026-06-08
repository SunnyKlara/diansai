/**
 * @file    main.c
 * @brief   2024 A AC-AC 斩波器并联 主程序
 */

#include "../config.h"
#include "../Drivers/pwm_chopper.h"
#include "../Drivers/zero_cross.h"
#include "../Drivers/adc_vio.h"
#include "../Drivers/oled.h"
#include "../Algorithm/ac_chopper.h"
#include "../Algorithm/voltage_loop.h"

typedef enum { ST_IDLE, ST_RUN, ST_FAULT } sys_state_t;

static volatile sys_state_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static float s_vset = 24.0f;
static vloop_t s_vloop;
static volatile uint8_t s_in_commutation = 0;
static volatile uint8_t s_comm_step = 0;
static volatile chop_half_t s_next_half = CHOP_HALF_POSITIVE;

static void _hal_clock_init(void) {}
static void _hal_gpio_init(void) {}

/* 20kHz PWM 中断：写 PWM */
void main_pwm_isr(void)
{
    chop_pwm_t pwm;
    if (s_state != ST_RUN) {
        chop_compute_pwm(CHOP_HALF_POSITIVE, 0.0f, &pwm);
        pwm_chopper_set(&pwm);
        return;
    }

    /* 取最新 RMS */
    ac_rms_t rms;
    adc_vio_get_rms(&rms);

    /* 电压外环（10ms 一次，但调用频次无所谓，PI 自带积分） */
    float duty = vloop_update(&s_vloop, rms.vout_rms, rms.iout_rms);

    /* 换流期间走 4 步序列 */
    if (s_in_commutation) {
        chop_step_commutation(s_comm_step, s_next_half, duty, &pwm);
        s_comm_step++;
        if (s_comm_step >= 4) {
            s_in_commutation = 0;
        }
    } else {
        chop_compute_pwm(zcd_get_polarity() ? CHOP_HALF_POSITIVE : CHOP_HALF_NEGATIVE,
                         duty, &pwm);
    }
    pwm_chopper_set(&pwm);
}

/* 过零中断：触发四步换流 */
void main_zcd_isr(void)
{
    s_in_commutation = 1;
    s_comm_step = 0;
    s_next_half = zcd_get_polarity() ? CHOP_HALF_POSITIVE : CHOP_HALF_NEGATIVE;
}

/* SysTick 1ms */
void main_systick_isr(void)
{
    s_tick_ms++;
}

static void check_fault(const ac_rms_t *rms)
{
    if ((rms->iout_rms > IOUT_OVER_A) ||
        (rms->vout_rms > VOUT_OVER_V)) {
        pwm_chopper_emergency_off();
        s_state = ST_FAULT;
    }
}

int main(void)
{
    _hal_clock_init();
    _hal_gpio_init();
    pwm_chopper_init();
    zcd_init();
    adc_vio_init();
    oled_init();
    vloop_init(&s_vloop);
    vloop_set_target(&s_vloop, s_vset, VIN_NOMINAL_VAC);

    adc_vio_auto_zero();
    adc_vio_start();
    pwm_chopper_start();
    s_state = ST_RUN;

    uint32_t last_oled = 0;
    while (1) {
        if ((s_tick_ms - last_oled) > 200) {
            last_oled = s_tick_ms;
            ac_rms_t rms; adc_vio_get_rms(&rms);
            check_fault(&rms);
            oled_show_status(s_vset, rms.vout_rms, rms.iout_rms, s_vloop.integ);
        }
    }
}
