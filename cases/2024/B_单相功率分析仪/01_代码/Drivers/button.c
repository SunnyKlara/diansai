#include "button.h"
/* TODO_TI: driverlib */

static uint8_t s_last[2] = { 1, 1 };

static uint8_t read(uint8_t idx) { (void)idx; return 1; }

void Button_Init(void)
{
    /* TODO_TI: 2 个按键 GPIO 输入 + 内部上拉 + 中断唤醒（用于 LPM 出口）*/
    s_last[0] = s_last[1] = 1u;
}

KeyId Button_Scan(void)
{
    KeyId r = KEY_NONE;
    for (uint8_t i = 0; i < 2u; i++) {
        uint8_t now = read(i);
        if (s_last[i] == 1u && now == 0u) r = (KeyId)(i + 1);
        s_last[i] = now;
    }
    return r;
}
