/**
 * @file    button.c
 * @brief   按键扫描占位实现 + 软件去抖（计数 3 次稳定才确认）
 */

#include "button.h"

#define DEBOUNCE_COUNT  3U

static volatile uint8_t s_raw[4]      = {1, 1, 1, 1};   /* 上拉默认高 */
static volatile uint8_t s_stable[4]   = {1, 1, 1, 1};
static volatile uint8_t s_count[4]    = {0, 0, 0, 0};
static volatile button_event_t s_event = BTN_NONE;

/* HAL 占位：读取 GPIO PE0..PE3 上拉输入 */
static uint8_t _hal_read(uint8_t i)
{
    (void)i;
    return 1;   /* 真机替换为 HAL_GPIO_ReadPin */
}

void button_init(void)
{
    for (int i = 0; i < 4; i++) {
        s_raw[i]    = 1;
        s_stable[i] = 1;
        s_count[i]  = 0;
    }
    s_event = BTN_NONE;
}

void button_scan(void)
{
    static const button_event_t MAP[4] = {
        BTN_MODE, BTN_APF_ON, BTN_APF_OFF, BTN_FAULT_RESET
    };

    for (int i = 0; i < 4; i++) {
        uint8_t now = _hal_read((uint8_t)i);
        if (now != s_raw[i]) {
            s_count[i] = 0;
            s_raw[i]   = now;
        } else if (s_count[i] < DEBOUNCE_COUNT) {
            s_count[i]++;
            if (s_count[i] == DEBOUNCE_COUNT) {
                /* 稳定状态变化：只有从 1→0（按下沿）才触发事件 */
                if ((s_stable[i] == 1) && (s_raw[i] == 0)) {
                    s_event = MAP[i];
                }
                s_stable[i] = s_raw[i];
            }
        }
    }
}

button_event_t button_get_event(void)
{
    button_event_t e = s_event;
    s_event = BTN_NONE;
    return e;
}
