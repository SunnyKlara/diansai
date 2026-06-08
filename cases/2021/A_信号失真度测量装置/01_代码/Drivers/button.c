#include "button.h"
/* TODO_TI: #include <ti/devices/msp432p4xx/driverlib/driverlib.h> */

void Button_Init(void)
{
    /* TODO_TI: P1.1 配置为 GPIO 输入 + 内部上拉（LaunchPad S1）*/
}

uint8_t Button_IsStartPressed(void)
{
    /* TODO_TI:
     *   return (MAP_GPIO_getInputPinValue(GPIO_PORT_P1, GPIO_PIN1) == 0) ? 1 : 0;
     */
    return 0;
}
