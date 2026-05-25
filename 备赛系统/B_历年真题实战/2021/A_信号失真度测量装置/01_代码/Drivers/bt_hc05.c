/**
 * @file    bt_hc05.c
 * @brief   HC-05 蓝牙占位实现（UART UCA2）
 */

#include "bt_hc05.h"
/* TODO_TI: #include <ti/devices/msp432p4xx/driverlib/driverlib.h> */

void BT_Init(void) { /* TODO_TI: UCA2 配置 115200 8N1 */ }

void BT_SendString(const char* str)
{
    (void)str;
    /* TODO_TI:
     *   while (*str) {
     *       MAP_UART_transmitData(EUSCI_A2_BASE, *str);
     *       str++;
     *   }
     */
}
