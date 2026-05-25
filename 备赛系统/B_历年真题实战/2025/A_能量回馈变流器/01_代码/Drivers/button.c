#include "button.h"
/* TODO_HAL: #include "stm32g4xx_hal.h" */

static uint8_t s_last_state[5] = { 1, 1, 1, 1, 1 };

static uint8_t read_pin(uint8_t idx)
{
    /* TODO_HAL: 根据 idx 返回 PC0~PC4 的电平 */
    (void)idx;
    return 1u;
}

void Button_Init(void)
{
    /* TODO_HAL: PC0~PC4 → GPIO 输入 + 上拉 */
    for (uint8_t i = 0; i < 5u; i++) s_last_state[i] = 1u;
}

KeyId Button_Scan(void)
{
    KeyId result = KEY_NONE;
    for (uint8_t i = 0; i < 5u; i++) {
        uint8_t now = read_pin(i);
        if ((s_last_state[i] == 1u) && (now == 0u)) {
            result = (KeyId)(i + 1);   /* 1=FREQ_UP, 2=FREQ_DOWN, 3=START, 4=STOP, 5=TASK */
        }
        s_last_state[i] = now;
    }
    return result;
}
