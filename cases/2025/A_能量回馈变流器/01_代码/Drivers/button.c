/**
 * @file    button.c
 * @brief   STM32G474 5 按键扫描（PC0~PC4）
 */

#include "button.h"
#include "stm32g4xx_hal.h"

static uint8_t s_last_state[5] = {1, 1, 1, 1, 1};

static const uint16_t s_pin_mask[5] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3, GPIO_PIN_4
};

static uint8_t read_pin(uint8_t idx)
{
    if (idx >= 5u) return 1u;
    return (HAL_GPIO_ReadPin(GPIOC, s_pin_mask[idx]) == GPIO_PIN_SET) ? 1u : 0u;
}

void Button_Init(void)
{
    /* CubeMX 已配置 PC0~PC4 为输入 + 上拉。这里清状态。 */
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
