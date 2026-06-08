#include "openmv_uart.h"
static volatile uint8_t s_room = 0;
static volatile uint8_t s_new = 0;

void omv_init(void) {}
void omv_on_uart_byte(uint8_t b)
{
    if (b >= '1' && b <= '8') { s_room = b - '0'; s_new = 1; }
}
uint8_t omv_get_room(uint8_t *room)
{
    if (!s_new || !room) return 0;
    *room = s_room; s_new = 0; return 1;
}
