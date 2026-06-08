/**
 * @file    main.c
 * @brief   2022 C 小车跟随行驶系统 主程序
 *          编译时通过 -DCAR_LEAD 或 -DCAR_FOLLOW 区分两车角色
 */
#include "../config.h"
#include "../Drivers/motor_msp.h"
#include "../Drivers/ir_line.h"
#include "../Drivers/ultrasonic.h"
#include "../Drivers/nrf_link.h"
#include "../Algorithm/line_pid.h"
#include "../Algorithm/dist_pid.h"

typedef enum { ST_IDLE, ST_RUN, ST_STOP_WAIT } st_t;

static volatile st_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static line_pid_t s_lpid;
static dist_pid_t s_dpid;

void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
    if (s_state == ST_IDLE) { motor_brake(); return; }

    float err = ir_line_get_error();
    float steer = line_pid_update(&s_lpid, err);

#ifdef CAR_LEAD
    int16_t base = (int16_t)(700);
    if (s_state == ST_STOP_WAIT) base = 0;
    motor_set(base - (int16_t)steer, base + (int16_t)steer);
#else
    /* CAR_FOLLOW */
    float dist = ultra_get_cm();
    float v_target = dist_pid_update(&s_dpid, dist);
    int16_t base = (int16_t)(v_target * 1000.0f);   /* 简化：mm/s ~ PWM */
    motor_set(base - (int16_t)steer, base + (int16_t)steer);
#endif

    if (ir_line_at_stop_marker()) {
        s_state = ST_STOP_WAIT;
    }
}

int main(void)
{
    motor_init();
    ir_line_init();
    ultra_init();
    line_pid_init(&s_lpid);
    dist_pid_init(&s_dpid);

#ifdef CAR_LEAD
    nrf_link_init(1);
#else
    nrf_link_init(2);
#endif

    s_state = ST_RUN;

    uint32_t last_tx = 0, last_us = 0;
    while (1) {
#ifdef CAR_LEAD
        if ((s_tick_ms - last_tx) > 50) {
            last_tx = s_tick_ms;
            lead_pkt_t pkt = {NRF_HEADER, CMD_SPEED, 700, 0, 0, 0};
            nrf_link_send(&pkt);
        }
#else
        lead_pkt_t pkt;
        if (nrf_link_recv(&pkt)) {
            /* 根据领头车命令调整 */
        }
        if ((s_tick_ms - last_us) > 50) {
            last_us = s_tick_ms;
            ultra_trigger();
        }
#endif
        if (s_state == ST_STOP_WAIT && ((s_tick_ms - last_tx) > 5000)) {
            s_state = ST_RUN;
        }
    }
}
