/**
 * @file    main_aim.c
 * @brief   2025 E 瞄准 MCU 主程序：OpenMV → PID → 二轴云台
 */
#include "../config.h"
#include "../Drivers/servo_aim.h"
#include "../Drivers/openmv_in.h"
#include "../Drivers/uart_link.h"
#include "../Algorithm/aim_pid.h"

typedef enum { ST_WAIT_CAR, ST_AIMING, ST_HIT, ST_DONE } st_t;

static volatile st_t s_state = ST_WAIT_CAR;
static volatile uint32_t s_tick_ms = 0;
static aim_pid_t s_pid;
static volatile uint32_t s_lock_count = 0;

void main_aim_systick_isr(void){ s_tick_ms++; }

void main_aim_ctrl_isr(void)
{
    if (s_state != ST_AIMING) return;
    int16_t cx, cy;
    if (!omv_get(&cx, &cy)) return;

    float dyaw, dpit;
    aim_update(&s_pid, cx, cy, &dyaw, &dpit);
    float yy, pp; servo_get(&yy, &pp);
    servo_set(yy + dyaw, pp + dpit);

    int16_t ex = cx - IMG_CX; if (ex<0) ex=-ex;
    int16_t ey = cy - IMG_CY; if (ey<0) ey=-ey;
    if (ex < AIM_TARGET_TOL_PX && ey < AIM_TARGET_TOL_PX) {
        s_lock_count++;
        if (s_lock_count > 30) {            /* 锁定 0.3s */
            s_state = ST_HIT;
        }
    } else {
        s_lock_count = 0;
    }
}

int main(void)
{
    servo_init(); omv_init(); uart_link_init();
    aim_init(&s_pid);
    s_state = ST_WAIT_CAR;

    while (1) {
        if (s_state == ST_WAIT_CAR) {
            uint8_t b;
            if (uart_link_recv(&b) && (b == MSG_CAR_READY)) {
                s_state = ST_AIMING;
                s_lock_count = 0;
            }
        } else if (s_state == ST_HIT) {
            uart_link_send(MSG_AIM_DONE);
            s_state = ST_DONE;
        }
    }
}
