#include "nrf24.h"
static uint8_t s_id = 0;
static volatile uint8_t s_recv_new = 0;
static volatile nrf_msg_t s_recv_msg;

void nrf24_init(uint8_t id){ s_id = id; }
void nrf24_send(const nrf_msg_t *msg){ (void)msg; /* SPI 发包 */ }
uint8_t nrf24_recv(nrf_msg_t *msg)
{
    if (!s_recv_new || !msg) return 0;
    *msg = s_recv_msg; s_recv_new = 0; return 1;
}
