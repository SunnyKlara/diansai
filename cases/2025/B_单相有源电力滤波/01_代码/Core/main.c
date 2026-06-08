/**
 * @file    main.c
 * @brief   2025 B 单相 APF 主程序 —— 5 状态机 + 中断挂接
 *
 * 状态：
 *   ST_IDLE        上电默认，等待按 MODE 进入测量
 *   ST_PRECHARGE   母线电容预充（等 Vbus > 阈值）
 *   ST_MEAS_ONLY   基本要求：测 uS / iL / 谐波，不输出 APF
 *   ST_APF_RUN    发挥：APF 实时补偿
 *   ST_FAULT       过流 / 欠压 / 过压
 */

#include "../config.h"
#include "../Drivers/adc_us_il.h"
#include "../Drivers/pwm_apf.h"
#include "../Drivers/lcd_tft.h"
#include "../Drivers/button.h"
#include "../Algorithm/apf_compensator.h"
#include "../Algorithm/harmonic_fft.h"

#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 状态机
 * ===================================================================== */

typedef enum {
    ST_IDLE      = 0,
    ST_PRECHARGE = 1,
    ST_MEAS_ONLY = 2,
    ST_APF_RUN   = 3,
    ST_FAULT     = 4
} sys_state_t;

static volatile sys_state_t s_state    = ST_IDLE;
static volatile uint32_t    s_tick_ms  = 0;     /* 系统毫秒计数             */

/* 算法实例 */
static apf_extractor_t  s_extractor;
static apf_pi_t         s_pi;

/* 谐波分析帧缓冲 */
static uint16_t          s_fft_frame[HARMONIC_FFT_SIZE];
static harmonic_result_t s_harm_result;
static volatile uint8_t  s_need_fft = 0;

/* 故障信息 */
typedef struct {
    uint8_t  vbus_over;
    uint8_t  vbus_under;
    uint8_t  if_over;
    uint32_t enter_tick_ms;
} fault_t;
static fault_t s_fault;

/* ========================================================================
 * HAL 钩子（占位）
 * ===================================================================== */

static void _hal_clock_init(void)   {}
static void _hal_gpio_init(void)    {}
static void _hal_systick_start(void){}
static float _hal_read_vbus_v(void) { return 48.0f; }   /* 真机：单 ADC 通道 + 分压 */
static void _hal_relay_precharge(uint8_t on) { (void)on; }

/* ========================================================================
 * 状态切换
 * ===================================================================== */

static const char *s_state_name(sys_state_t st)
{
    switch (st) {
        case ST_IDLE:      return "IDLE";
        case ST_PRECHARGE: return "PRECHARGE";
        case ST_MEAS_ONLY: return "MEAS";
        case ST_APF_RUN:   return "APF";
        case ST_FAULT:     return "FAULT";
        default:           return "?";
    }
}

static void enter_state(sys_state_t next)
{
    if (next == s_state) {
        return;
    }

    /* 离开旧状态 */
    if (s_state == ST_APF_RUN) {
        pwm_apf_stop();
        apf_pi_disable(&s_pi);
    }

    /* 进入新状态 */
    switch (next) {
        case ST_IDLE:
            pwm_apf_stop();
            adc_us_il_stop();
            break;

        case ST_PRECHARGE:
            _hal_relay_precharge(0);   /* 串入限流电阻 */
            adc_us_il_start();
            s_fault.enter_tick_ms = s_tick_ms;
            break;

        case ST_MEAS_ONLY:
            _hal_relay_precharge(1);   /* 短路限流电阻 */
            pwm_apf_stop();
            adc_us_il_start();
            break;

        case ST_APF_RUN:
            apf_pi_soft_start(&s_pi);
            pwm_apf_start();
            break;

        case ST_FAULT:
            pwm_apf_emergency_off();
            s_fault.enter_tick_ms = s_tick_ms;
            break;
    }

    s_state = next;
    lcd_tft_show_mode(s_state_name(next));
}

/* ========================================================================
 * 故障检查
 * ===================================================================== */

static uint8_t check_fault(const adc_sample_t *s)
{
    float vbus = _hal_read_vbus_v();

    s_fault.vbus_over  = (vbus > VBUS_OVER_V)  ? 1 : 0;
    s_fault.vbus_under = (vbus < VBUS_UNDER_V) ? 1 : 0;

    float i_f_abs = s->i_f >= 0 ? s->i_f : -s->i_f;
    s_fault.if_over    = (i_f_abs > IF_MAX_INSTANT_A) ? 1 : 0;

    return (s_fault.vbus_over | s_fault.vbus_under | s_fault.if_over);
}

/* ========================================================================
 * 控制环：20 kHz（TIM1 UPDATE ISR 内调用）
 * ===================================================================== */

void main_ctrl_loop_isr(void)
{
    adc_sample_t smp;
    if (adc_us_il_get_sample(&smp) == 0) {
        return;
    }

    /* 任何状态都做故障检测 */
    if (check_fault(&smp)) {
        if (s_state == ST_APF_RUN) {
            enter_state(ST_FAULT);
        }
    }

    if (s_state == ST_APF_RUN) {
        /* 1. 基波提取，得到 iF_ref */
        float if_ref = apf_extractor_update(&s_extractor, smp.il);

        /* 2. 电流跟踪 PI → 占空比 */
        float duty = apf_pi_update(&s_pi, if_ref, smp.i_f);

        /* 3. 写 PWM */
        pwm_apf_set_duty(duty);
    } else if (s_state == ST_MEAS_ONLY) {
        /* 测量模式仍维护基波提取器（下次开 APF 即可立即用） */
        (void)apf_extractor_update(&s_extractor, smp.il);
    }
}

/* ========================================================================
 * 慢速循环：5 Hz LCD + 谐波 FFT 触发
 * ===================================================================== */

static void slow_loop_5hz(void)
{
    /* 触发一次 FFT 采样帧 */
    if (s_need_fft == 0) {
        s_need_fft = 1;
    }

    /* 帧到位 → 跑 FFT */
    if (adc_us_il_get_frame(s_fft_frame, HARMONIC_FFT_SIZE)) {
        harmonic_analyze(s_fft_frame,
                         ADC_IL_SCALE_A_PER_LSB,
                         ADC_IL_OFFSET_LSB,
                         &s_harm_result);
        s_need_fft = 0;
        lcd_tft_show_harmonic(&s_harm_result);
    }
}

/* ========================================================================
 * 50 Hz 按键扫描 + 状态切换
 * ===================================================================== */

static void slow_loop_50hz(void)
{
    button_scan();
    button_event_t e = button_get_event();

    switch (s_state) {
        case ST_IDLE:
            if (e == BTN_MODE) enter_state(ST_PRECHARGE);
            break;

        case ST_PRECHARGE: {
            float v = _hal_read_vbus_v();
            if (v > VBUS_PRECHARGE_V) {
                enter_state(ST_MEAS_ONLY);
            } else if ((s_tick_ms - s_fault.enter_tick_ms) > PRECHARGE_TIMEOUT_MS) {
                enter_state(ST_FAULT);
            }
            break;
        }

        case ST_MEAS_ONLY:
            if (e == BTN_APF_ON)  enter_state(ST_APF_RUN);
            if (e == BTN_MODE)    enter_state(ST_IDLE);
            break;

        case ST_APF_RUN:
            if (e == BTN_APF_OFF) enter_state(ST_MEAS_ONLY);
            break;

        case ST_FAULT:
            if (e == BTN_FAULT_RESET) {
                if ((s_tick_ms - s_fault.enter_tick_ms) > FAULT_RECOVER_MS) {
                    enter_state(ST_IDLE);
                }
            }
            break;
    }
}

/* ========================================================================
 * 主循环
 * ===================================================================== */

int main(void)
{
    _hal_clock_init();
    _hal_gpio_init();

    /* 驱动层 */
    adc_us_il_init();
    pwm_apf_init();
    lcd_tft_init();
    button_init();

    /* 算法层 */
    harmonic_init();
    apf_extractor_init(&s_extractor);
    apf_pi_init(&s_pi);

    /* 静态环境零点自校 */
    adc_us_il_auto_zero();

    /* 启动 SysTick → 1 ms */
    _hal_systick_start();

    lcd_tft_clear();
    enter_state(ST_IDLE);

    uint32_t last_5hz_tick  = 0;
    uint32_t last_50hz_tick = 0;

    while (1) {
        /* 5 Hz */
        if ((s_tick_ms - last_5hz_tick) >= (1000U / LCD_REFRESH_HZ)) {
            last_5hz_tick = s_tick_ms;
            slow_loop_5hz();
        }
        /* 50 Hz */
        if ((s_tick_ms - last_50hz_tick) >= (1000U / BUTTON_SCAN_HZ)) {
            last_50hz_tick = s_tick_ms;
            slow_loop_50hz();
        }
    }
}

/* SysTick 1ms（CubeMX 默认 HAL_IncTick 已经在跑，这里也 hook 一次） */
void main_systick_isr(void)
{
    s_tick_ms++;
}
