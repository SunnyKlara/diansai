/**
 * @file    led_buzzer.c
 * @brief   MSPM0G3507 LED + 蜂鸣器
 */

#include "led_buzzer.h"
#include <ti/driverlib/driverlib.h>
#include "ti_msp_dl_config.h"

/* SysConfig 实例宏：
 *   GPIO_LED_PORT, GPIO_LED_LED_R_PIN, LED_G_PIN, LED_B_PIN
 *   GPIO_BUZ_PORT, GPIO_BUZ_BUZZER_PIN
 */

static const uint32_t s_led_mask[3] = {
    GPIO_LED_LED_R_PIN, GPIO_LED_LED_G_PIN, GPIO_LED_LED_B_PIN
};

void LED_Init(void)
{
    /* SysConfig 已配置成输出 + 初值 0。无需额外动作。 */
    DL_GPIO_clearPins(GPIO_LED_PORT,
        GPIO_LED_LED_R_PIN | GPIO_LED_LED_G_PIN | GPIO_LED_LED_B_PIN);
}

void LED_On(LedId id)
{
    if ((uint8_t)id >= 3) return;
    DL_GPIO_setPins(GPIO_LED_PORT, s_led_mask[(uint8_t)id]);
}

void LED_Off(LedId id)
{
    if ((uint8_t)id >= 3) return;
    DL_GPIO_clearPins(GPIO_LED_PORT, s_led_mask[(uint8_t)id]);
}

void LED_Toggle(LedId id)
{
    if ((uint8_t)id >= 3) return;
    DL_GPIO_togglePins(GPIO_LED_PORT, s_led_mask[(uint8_t)id]);
}

void Buzzer_Init(void)
{
    DL_GPIO_clearPins(GPIO_BUZ_PORT, GPIO_BUZ_BUZZER_PIN);
}

void Buzzer_BeepBlocking(uint16_t ms)
{
    DL_GPIO_setPins(GPIO_BUZ_PORT, GPIO_BUZ_BUZZER_PIN);
    /* 简易延时：80MHz × ms × 1000 / 4 cycles ≈ 20000 × ms 个空循环
     * 真机用 SysTick 替换更准。
     */
    delay_cycles((uint32_t)20000U * (uint32_t)ms);
    DL_GPIO_clearPins(GPIO_BUZ_PORT, GPIO_BUZ_BUZZER_PIN);
}
