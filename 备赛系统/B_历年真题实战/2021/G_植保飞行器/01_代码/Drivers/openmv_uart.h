#ifndef __OPENMV_UART_H__
#define __OPENMV_UART_H__
#include <stdint.h>
#include "../Algorithm/spray_planner.h"

void omv_init(void);
void omv_on_byte(uint8_t b);
uint8_t omv_get_target(target_t *t);
#endif
