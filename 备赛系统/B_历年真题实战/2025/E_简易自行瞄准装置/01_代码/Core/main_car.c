/**
 * @file    main_car.c
 * @brief   2025 E 小车 MCU 主程序：循迹 + 到位通知
 */
#include "../config.h"
#include "../Drivers/motor.h"
#include "../Drivers/ir_line.h"
#include "../Drivers/uart_link.h"
#include "../Algorithm/line_pid.h"

typedef enum { ST_IDLE, ST_DRIVE, ST_ARRIVED, ST_DONE } st_t;

static volatile st_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static line_pid_t s_pid;

void main_car_systick_isr(void){ s_tick_ms++; }

void main_car_ctrl_isr(void)
{
    if (s_state == ST_IDLE || s_state == ST_DONE) { motor_brake(); return; }
    if (s_state == ST_ARRIVED) {
        motor_brake();
        return;
    }

    float err   = ir_line_get_error();
    float steer = line_pid_update(&s_pid, err);
    int16_t base = (int16_t)(700);
    motor_set(base - (int16_t)steer, base + (int16_t)steer);

    if (ir_line_at_target()) {
        motor_brake();
        s_state = ST_ARRIVED;
        uart_link_send(MSG_CAR_READY);
    }
}

int main(void)
{
    motor_init(); ir_line_init(); uart_link_init();
    line_pid_init(&s_pid);
    s_state = ST_DRIVE;

    while (1) {
        if (s_state == ST_ARRIVED) {
            uint8_t b;
            if (uart_link_recv(&b) && (b == MSG_AIM_DONE)) {
                s_state = ST_DONE;
            }
        }
    }
}
