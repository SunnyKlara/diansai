/**
 * @file    main.c
 * @brief   2021 F 智能送药小车 主程序
 */

#include "../config.h"
#include "../Drivers/motor.h"
#include "../Drivers/encoder.h"
#include "../Drivers/ir_line.h"
#include "../Drivers/openmv_uart.h"
#include "../Drivers/nrf24.h"
#include "../Algorithm/line_pid.h"
#include "../Algorithm/path_planner.h"

typedef enum { ST_IDLE, ST_DETECT, ST_DRIVE, ST_DELIVER, ST_RETURN, ST_DONE } st_t;

static volatile st_t s_state = ST_IDLE;
static volatile uint32_t s_tick_ms = 0;
static line_pid_t s_pid;
static path_planner_t s_path;

void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
    if (s_state == ST_IDLE || s_state == ST_DONE) {
        motor_brake();
        return;
    }

    float err = ir_line_get_error();
    uint8_t at_t = ir_line_at_t_node();

    if (s_state == ST_DRIVE) {
        path_action_t act = path_next_action(&s_path, at_t, 0);
        switch (act) {
            case ACT_FORWARD: {
                float steer = line_pid_update(&s_pid, err);
                int16_t base = (int16_t)(MOTOR_MAX_PWM * 0.6f);
                motor_set(base - (int16_t)steer, base + (int16_t)steer);
                break;
            }
            case ACT_TURN_LEFT:  motor_set(-300, 600); break;
            case ACT_TURN_RIGHT: motor_set(600, -300); break;
            case ACT_STOP:
                motor_brake();
                s_state = ST_DELIVER;
                break;
            default: motor_brake(); break;
        }
    }
}

int main(void)
{
    motor_init();
    enc_init();
    ir_line_init();
    omv_init();
    nrf24_init(CAR_ID_SELF);
    line_pid_init(&s_pid);
    path_init(&s_path);

    s_state = ST_DETECT;

    while (1) {
        if (s_state == ST_DETECT) {
            uint8_t room;
            if (omv_get_room(&room)) {
                path_set_target(&s_path, room);
                s_state = ST_DRIVE;
                line_pid_reset(&s_pid);
            }
        } else if (s_state == ST_DELIVER) {
            /* 等微动开关触发 */
            if ((s_tick_ms % 1000) == 0) s_state = ST_RETURN;
        } else if (s_state == ST_RETURN) {
            s_state = ST_DONE;
        }
    }
}
