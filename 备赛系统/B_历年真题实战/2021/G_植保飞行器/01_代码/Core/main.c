/**
 * @file    main.c
 * @brief   2021 G 植保飞行器 任务 MCU
 *          飞控由 Pixhawk 主导，本 MCU 处理视觉 + 喷洒 + 通信
 */
#include "../config.h"
#include "../Drivers/openmv_uart.h"
#include "../Drivers/lora_module.h"
#include "../Drivers/pump_pwm.h"
#include "../Algorithm/spray_planner.h"

static spray_planner_t s_planner;

int main(void)
{
    omv_init();
    lora_init();
    pump_init();
    spray_init(&s_planner);

    while (1) {
        target_t t;
        if (omv_get_target(&t)) {
            spray_on_target(&s_planner, &t);
            if (spray_should_pump(&s_planner)) {
                pump_set(1);
                /* 喷洒 SPRAY_DURATION_MS 后关 */
            }
        }
    }
}
