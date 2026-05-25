#include "nrf_link.h"
static lead_pkt_t s_recv;
static volatile uint8_t s_new = 0;
void nrf_link_init(uint8_t id){ (void)id; }
void nrf_link_send(const lead_pkt_t *p){ (void)p; }
uint8_t nrf_link_recv(lead_pkt_t *p){ if (!p || !s_new) return 0; *p = s_recv; s_new = 0; return 1; }
