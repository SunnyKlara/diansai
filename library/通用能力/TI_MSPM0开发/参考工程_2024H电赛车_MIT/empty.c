/*
 * Copyright (c) 2021, Texas Instruments Incorporated
 * All rights reserved.
 */

#include "ti_msp_dl_config.h"

#include "APP/app_bringup.h"
#include "BSP/bsp_encoder.h"
#include "BSP/bsp_gray.h"
#include "BSP/bsp_imu.h"
#include "BSP/bsp_motor.h"
#include "BSP/bsp_timebase.h"
#include "BSP/bsp_uart.h"
#include "BSP/bsp_user.h"

int main(void)
{
    SYSCFG_DL_init();

    bsp_motor_init();
    bsp_encoder_init();
    bsp_gray_init();
    (void) bsp_imu_init();
    bsp_uart_init();
    bsp_user_init();
    bsp_timebase_init();
    app_bringup_init();

    while (1) {
        app_bringup_poll();

        while (bsp_timebase_take_tick()) {
            app_bringup_tick_1ms();
            app_bringup_poll();
        }

        __WFI();
    }
}
