/**
 * @file    led_buzzer.h
 * @brief   3 LED + 1 蜂鸣，状态指示
 */

#ifndef __LED_BUZZER_H
#define __LED_BUZZER_H

#include <stdint.h>

typedef enum {
    LED_RED = 0,
    LED_GREEN,
    LED_YELLOW,
} LedId;

void LED_Init(void);
void LED_On(LedId id);
void LED_Off(LedId id);
void LED_Toggle(LedId id);

void Buzzer_Init(void);
/** @brief 蜂鸣 ms 毫秒（阻塞）*/
void Buzzer_BeepBlocking(uint16_t ms);

#endif /* __LED_BUZZER_H */
