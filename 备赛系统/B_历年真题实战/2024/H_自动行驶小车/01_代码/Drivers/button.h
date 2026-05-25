/**
 * @file    button.h
 * @brief   4 路按键：START / STOP / TASK / RESET
 *  - 上拉接法：按下时引脚电平为 0
 *  - 软件 20ms 消抖
 */

#ifndef __BUTTON_H
#define __BUTTON_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_START,
    KEY_STOP,
    KEY_TASK,
    KEY_RESET,
} KeyId;

void  Button_Init(void);

/**
 * @brief 扫描按键状态（20ms 调用），返回本次按下的键
 *  - 仅在按下边沿（上次未按下，本次按下）时返回非 NONE
 */
KeyId Button_Scan(void);

#endif /* __BUTTON_H */
