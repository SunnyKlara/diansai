#include "BSP/bsp_user.h"

#include "ti_msp_dl_config.h"

void bsp_user_init(void)
{
    DL_GPIO_clearPins(GPIO_USER_PORT, GPIO_USER_LED_PIN);
}

bool bsp_user_button_pressed(void)
{
    return (DL_GPIO_readPins(GPIO_USER_PORT, GPIO_USER_BUTTON_PIN) == 0U);
}

void bsp_user_led_toggle(void)
{
    DL_GPIO_togglePins(GPIO_USER_PORT, GPIO_USER_LED_PIN);
}
