#ifndef __OPENMV_UART_H__
#define __OPENMV_UART_H__
#include <stdint.h>
void omv_init(void);
void omv_on_uart_byte(uint8_t b);
uint8_t omv_get_room(uint8_t *room);
#endif
