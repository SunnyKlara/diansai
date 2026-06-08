/**
 * @file    main.c
 * @brief   多点温度采集系统 主程序
 *
 *  状态机：
 *      ST_BOOT      上电初始化
 *      ST_MEASURE   测量
 *      ST_PROCESS   计算
 *      ST_DISPLAY   显示
 *      ST_LPM       低功耗
 */

#include <stdint.h>
#include "../config.h"

/* TODO_PORT: include hardware-specific headers here */

int main(void) {
    /* TODO_PORT: clock + interrupts + LPM init */
    
    while (1) {
        /* run_measure_cycle(); */
        /* UART_PollCommands(); */
    }
}
