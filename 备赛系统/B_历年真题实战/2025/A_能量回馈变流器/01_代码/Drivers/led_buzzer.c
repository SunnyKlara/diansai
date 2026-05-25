#include "led_buzzer.h"
/* TODO_HAL: #include "stm32g4xx_hal.h" */

void LED_Init(void)     { /* TODO_HAL */ }
void LED_On(LedId id)   { (void)id; /* TODO_HAL */ }
void LED_Off(LedId id)  { (void)id; /* TODO_HAL */ }
void LED_Toggle(LedId id){ (void)id; /* TODO_HAL */ }

void Buzzer_Init(void) { /* TODO_HAL */ }
void Buzzer_BeepBlocking(uint16_t ms) { (void)ms; /* TODO_HAL: GPIO + delay */ }
