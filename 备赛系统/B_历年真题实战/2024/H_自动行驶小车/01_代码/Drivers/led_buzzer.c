#include "led_buzzer.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

void LED_Init(void) { /* TODO_MSPM0: PA21~PA23 推挽输出 */ }

void LED_On(LedId id)     { (void)id; /* TODO_MSPM0 */ }
void LED_Off(LedId id)    { (void)id; /* TODO_MSPM0 */ }
void LED_Toggle(LedId id) { (void)id; /* TODO_MSPM0 */ }

void Buzzer_Init(void) { /* TODO_MSPM0: PA24 推挽输出 */ }

void Buzzer_BeepBlocking(uint16_t ms)
{
    /* TODO_MSPM0:
     *   DL_GPIO_setPins(GPIOA, GPIO_PIN_24);
     *   delay_ms(ms);
     *   DL_GPIO_clearPins(GPIOA, GPIO_PIN_24);
     */
    (void)ms;
}
