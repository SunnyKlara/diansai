/**
 * @file    main.c
 * @brief   2021 B 三相 AC-DC 主程序：Vienna PWM 整流 + Buck 后级
 */

#include "../config.h"
#include "../Drivers/adc_threephase.h"
#include "../Drivers/pwm_vienna.h"
#include "../Drivers/oled.h"
#include "../Algorithm/park_clarke.h"
#include "../Algorithm/vienna_dq.h"
#include "../Algorithm/buck_loop.h"

typedef enum { ST_IDLE, ST_PRECHARGE, ST_RUN, ST_FAULT } st_t;

static volatile st_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static vienna_t s_vienna;
static buck_t   s_buck;
static pll_t    s_pll;

static void _hal_init(void){}
void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
    pq_sample_t s;
    if (!adc3p_get_sample(&s)) return;

    /* 1. abc → αβ */
    float va_a, va_b;
    clarke_transform(s.va, s.vb, s.vc, &va_a, &va_b);
    float ia_a, ia_b;
    clarke_transform(s.ia, s.ib, s.ic, &ia_a, &ia_b);

    /* 2. PLL 锁相到 Va */
    float vd_pll, vq_pll;
    park_transform(va_a, va_b, s_pll.theta, &vd_pll, &vq_pll);
    pll_update(&s_pll, vq_pll);

    /* 3. αβ → dq（电流） */
    float id, iq;
    park_transform(ia_a, ia_b, s_pll.theta, &id, &iq);

    /* 4. Vienna DQ 控制 → 调制比 */
    float md, mq;
    vienna_update(&s_vienna, s.vbus, id, iq, vd_pll, vq_pll, s_pll.omega,
                  &md, &mq);

    /* 5. dq → αβ → abc */
    float ma_a, ma_b, ma, mb, mc;
    inv_park_transform(md, mq, s_pll.theta, &ma_a, &ma_b);
    inv_clarke_transform(ma_a, ma_b, &ma, &mb, &mc);

    /* 6. 写 Vienna PWM */
    if (s_state == ST_RUN) {
        pwm_vienna_set(0.5f + 0.5f * ma, 0.5f + 0.5f * mb, 0.5f + 0.5f * mc);
    }

    /* 7. Buck 内环（每个 ISR 都跑） */
    float duty = buck_update(&s_buck, s.vout, s.il);
    if (s_state == ST_RUN) pwm_buck_set(duty);
}

static void enter_state(st_t next)
{
    if (next == s_state) return;
    if (next == ST_IDLE || next == ST_FAULT) pwm_emergency_off();
    if (next == ST_RUN)  { pwm_vienna_start(); pwm_buck_start(); }
    s_state = next;
}

int main(void)
{
    _hal_init();
    pwm_init();
    adc3p_init();
    oled_init();
    pll_init(&s_pll, 1.0f / (float)CTRL_FREQ_HZ);
    vienna_init(&s_vienna);
    buck_init(&s_buck);

    adc3p_auto_zero();
    adc3p_start();
    enter_state(ST_PRECHARGE);

    uint32_t last_oled = 0;
    while (1) {
        if (s_state == ST_PRECHARGE) {
            pq_sample_t s; adc3p_get_sample(&s);
            if (s.vbus > VBUS_PRECHARGE_V) enter_state(ST_RUN);
        }
        if ((s_tick_ms - last_oled) > 200) {
            last_oled = s_tick_ms;
            pq_sample_t s; adc3p_get_sample(&s);
            float pf = 0.0f, eta = 0.0f;
            oled_show(s.vout, s.il, s.vbus, pf, eta);
        }
    }
}
