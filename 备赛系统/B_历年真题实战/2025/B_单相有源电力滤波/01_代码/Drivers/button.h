/**
 * @file    button.h
 * @brief   4 路按键扫描（KD/KC/APF_ON_OFF/MODE）
 *
 * 资源映射（占位）：
 *   - PE0..PE3 → KEY1..KEY4 上拉输入
 *   - 50 Hz 扫描 + 软件去抖
 */

#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdint.h>

typedef enum {
    BTN_NONE     = 0,
    BTN_MODE     = 1,    /* IDLE → MEAS_ONLY 切换       */
    BTN_APF_ON   = 2,    /* MEAS_ONLY → APF_RUN         */
    BTN_APF_OFF  = 3,    /* APF_RUN → MEAS_ONLY         */
    BTN_FAULT_RESET = 4  /* FAULT → IDLE                */
} button_event_t;

void button_init(void);

/**
 * @brief 50 Hz 周期调用一次（在 TIM3 ISR 内）
 */
void button_scan(void);

/**
 * @brief 取最近一次按键事件（取出后清除）。
 * @return BTN_NONE 表示没有事件
 */
button_event_t button_get_event(void);

#endif /* __BUTTON_H__ */
