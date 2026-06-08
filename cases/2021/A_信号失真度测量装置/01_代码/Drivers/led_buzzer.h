/**
 * @file    led_buzzer.h
 * @brief   状态指示（LaunchPad LED1红 / LED2绿 + 蜂鸣器）
 *
 *  虽然 main.c 直接用 GPIO 接口控制，本头文件保留接口
 *  以便后续行为变化（如双闪、长鸣等）。
 */

#ifndef __LED_BUZZER_H
#define __LED_BUZZER_H

#include <stdint.h>

typedef enum { LED_BUSY = 0, LED_DONE } LedId;

void LED_Init(void);
void LED_On(LedId id);
void LED_Off(LedId id);

void Buzzer_Init(void);
void Buzzer_BeepBlocking(uint16_t ms);

#endif
