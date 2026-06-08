#ifndef __LED_BUZZER_H
#define __LED_BUZZER_H

#include <stdint.h>

typedef enum { LED_RED = 0, LED_GREEN, LED_YELLOW } LedId;

void LED_Init(void);
void LED_On(LedId id);
void LED_Off(LedId id);
void LED_Toggle(LedId id);

void Buzzer_Init(void);
void Buzzer_BeepBlocking(uint16_t ms);

#endif
