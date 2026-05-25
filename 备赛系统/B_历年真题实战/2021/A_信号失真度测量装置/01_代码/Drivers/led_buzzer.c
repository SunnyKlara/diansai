#include "led_buzzer.h"
/* TODO_TI: #include <ti/devices/msp432p4xx/driverlib/driverlib.h> */

void LED_Init(void)              { /* TODO_TI: P1.0 / P2.1 推挽输出 */ }
void LED_On(LedId id)            { (void)id; /* TODO_TI */ }
void LED_Off(LedId id)           { (void)id; /* TODO_TI */ }

void Buzzer_Init(void)           { /* TODO_TI: P2.4 推挽输出 */ }

void Buzzer_BeepBlocking(uint16_t ms)
{
    (void)ms;
    /* TODO_TI:
     *   MAP_GPIO_setOutputHighOnPin(BUZZER_PORT, BUZZER_PIN);
     *   MAP_SysCtl_delay(ms * (CPU_CLK / 3000));
     *   MAP_GPIO_setOutputLowOnPin(BUZZER_PORT, BUZZER_PIN);
     */
}
