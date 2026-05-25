/**
 * @file    main.c
 * @brief   2022 A 单相交流电子负载主程序
 */

#include "../config.h"
#include "../Drivers/adc_load.h"
#include "../Drivers/pwm_eload.h"
#include "../Algorithm/sogi_pll.h"
#include "../Algorithm/pf_control.h"

typedef enum { ST_IDLE, ST_RUN, ST_FAULT } st_t;

static volatile st_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static sogi_pll_t s_pll;
static pfc_t s_pfc;

void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
    load_sample_t smp;
    if (!adc_load_get(&smp)) return;

    /* 1. PLL 锁相 */
    float theta = sogi_pll_update(&s_pll, smp.v_grid);

    /* 2. PFC dq 控制 → 占空比 */
    if (s_state == ST_RUN) {
        float duty = pfc_update(&s_pfc, smp.i_grid, theta);
        pwm_rectifier_set(duty);

        /* 3. 后级回馈：恒定 SPWM 把母线能量回馈到电阻负载 */
        float spwm = 0.5f + 0.45f * sinf(theta);
        pwm_inverter_set(spwm);
    }
}

int main(void)
{
    pwm_init();
    adc_load_init();
    sogi_pll_init(&s_pll, CTRL_PERIOD_S);
    pfc_init(&s_pfc);
    pfc_set(&s_pfc, LOAD_R, 1.0f, I_AMP_DEFAULT_A);

    adc_load_start();
    pwm_rectifier_start();
    pwm_inverter_start();
    s_state = ST_RUN;

    while (1) {
        /* 按键扫描：调 cosφ / 切换 R/L/C 类型，省略 */
    }
}

#include <math.h>
