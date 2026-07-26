#ifndef BSP_USER_H
#define BSP_USER_H

#include <stdbool.h>

void bsp_user_init(void);
bool bsp_user_button_pressed(void);
void bsp_user_led_toggle(void);

#endif
