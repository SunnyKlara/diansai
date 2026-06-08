#ifndef __LED_H
#define __LED_H

#include <stdint.h>

typedef enum { LED_RUN = 0, LED_FAULT } LedId;

void LED_Init(void);
void LED_On(LedId id);
void LED_Off(LedId id);
void LED_Toggle(LedId id);

#endif
