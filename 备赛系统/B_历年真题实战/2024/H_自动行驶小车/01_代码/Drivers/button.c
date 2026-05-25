#include "button.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

static uint8_t s_last_state[4] = { 1, 1, 1, 1 };

static uint8_t read_pin(uint8_t idx)
{
    /* TODO_MSPM0: 根据 idx 读 PA18/PA19/PA20/PA21（按键）*/
    (void)idx;
    return 1; /* 默认未按下 */
}

void Button_Init(void)
{
    /* TODO_MSPM0: PA18~PA21 配置为 GPIO 输入 + 内部上拉 */
    for (int i = 0; i < 4; i++) s_last_state[i] = 1;
}

KeyId Button_Scan(void)
{
    KeyId result = KEY_NONE;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t now = read_pin(i);
        if (s_last_state[i] == 1 && now == 0) {
            /* 按下边沿 */
            result = (KeyId)(i + 1); /* 1=START, 2=STOP, 3=TASK, 4=RESET */
        }
        s_last_state[i] = now;
    }
    return result;
}
