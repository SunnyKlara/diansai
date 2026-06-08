/**
 * @file    pwm_vienna.c
 */
#include "pwm_vienna.h"
#include "../config.h"

static volatile uint8_t s_v_running = 0, s_b_running = 0;

static void _hal_init(void) { }
static void _hal_v_set(float a, float b, float c)  { (void)a;(void)b;(void)c; }
static void _hal_b_set(float d)                    { (void)d; }
static void _hal_v_start(void) { } static void _hal_v_stop(void) { }
static void _hal_b_start(void) { } static void _hal_b_stop(void) { }

void pwm_init(void) { _hal_init(); }
void pwm_vienna_start(void) { s_v_running = 1; _hal_v_start(); }
void pwm_vienna_stop(void)  { _hal_v_stop(); s_v_running = 0; }
void pwm_vienna_set(float a, float b, float c)
{
    if (!s_v_running) return;
    _hal_v_set(a, b, c);
}
void pwm_buck_start(void) { s_b_running = 1; _hal_b_start(); }
void pwm_buck_stop(void)  { _hal_b_stop(); s_b_running = 0; }
void pwm_buck_set(float d){ if (s_b_running) _hal_b_set(d); }
void pwm_emergency_off(void){ _hal_v_stop(); _hal_b_stop(); s_v_running = s_b_running = 0; }
