#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void bsp_uart_init(void);
bool bsp_uart_read_line(char *destination, size_t capacity);
void bsp_uart_write(const char *text);
void bsp_uart_write_char(char value);
void bsp_uart_write_u32(uint32_t value);
void bsp_uart_write_i32(int32_t value);
uint32_t bsp_uart_rx_count(void);

#endif
