#include "uart_link.h"
static volatile uint8_t s_rx_byte=0; static volatile uint8_t s_new=0;
void uart_link_init(void){}
void uart_link_send(uint8_t byte){ (void)byte; }
void uart_link_on_rx(uint8_t b){ s_rx_byte=b; s_new=1; }
uint8_t uart_link_recv(uint8_t *out){ if(!out||!s_new)return 0; *out=s_rx_byte; s_new=0; return 1; }
