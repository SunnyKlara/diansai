#ifndef __UART_LINK_H__
#define __UART_LINK_H__
#include <stdint.h>
void uart_link_init(void);
void uart_link_send(uint8_t byte);
void uart_link_on_rx(uint8_t b);
uint8_t uart_link_recv(uint8_t *out);
#endif
