/**
 * @file    button.h
 * @brief   启动按键（LaunchPad S1，P1.1）
 *
 *  虽然 main.c 直接用 GPIO 接口检测，本头文件保留以备
 *  后续添加多按键 / 长按检测等扩展。
 */

#ifndef __BUTTON_H
#define __BUTTON_H

#include <stdint.h>

void    Button_Init(void);
uint8_t Button_IsStartPressed(void);   /* 1 = 按下，0 = 未按 */

#endif
