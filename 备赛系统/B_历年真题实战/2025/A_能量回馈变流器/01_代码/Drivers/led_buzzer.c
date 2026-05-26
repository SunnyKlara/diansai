/**
 * @file    led_buzzer.c
 * @brief   STM32G474 LED + 蜂鸣器
 *
 * 资源映射：
 *   LED_R   → PB7
 *   LED_G   → PB0
 *   LED_B   → PB1（可选）
 *   BUZZER  → PB1
 */

#include "led_buzzer.h"
#include "stm32g4xx_hal.h"

static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} s_led_map[3] = {
    { GPIOB, GPIO_PIN_7 },   /* LED_R */
    { GPIOB, GPIO_PIN_0 },   /* LED_G */
    { GPIOB, GPIO_PIN_1 },   /* LED_B */
};

void LED_Init(void)
{
    /* CubeMX 已配置成 OUT。清初值。 */
    for (uint8_t i = 0; i < 3u; i++) {
        HAL_GPIO_WritePin(s_led_map[i].port, s_led_map[i].pin, GPIO_PIN_RESET);
    }
}

void LED_On(LedId id)
{
    if ((uint8_t)id >= 3u) return;
    HAL_GPIO_WritePin(s_led_map[(uint8_t)id].port,
                      s_led_map[(uint8_t)id].pin, GPIO_PIN_SET);
}

void LED_Off(LedId id)
{
    if ((uint8_t)id >= 3u) return;
    HAL_GPIO_WritePin(s_led_map[(uint8_t)id].port,
                      s_led_map[(uint8_t)id].pin, GPIO_PIN_RESET);
}

void LED_Toggle(LedId id)
{
    if ((uint8_t)id >= 3u) return;
    HAL_GPIO_TogglePin(s_led_map[(uint8_t)id].port,
                       s_led_map[(uint8_t)id].pin);
}

/* Buzzer 与 LED_B 共用 PB1（按硬件实际接线选择）*/
void Buzzer_Init(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
}

void Buzzer_BeepBlocking(uint16_t ms)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(ms);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
}
