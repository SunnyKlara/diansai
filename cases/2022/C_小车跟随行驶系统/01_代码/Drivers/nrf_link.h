#ifndef __NRF_LINK_H__
#define __NRF_LINK_H__
#include <stdint.h>

typedef struct {
    uint8_t header;
    uint8_t cmd;
    int16_t speed_mms;
    uint8_t lap;
    uint8_t track;       /* 0=外, 1=内 */
    uint8_t crc;
} lead_pkt_t;

void nrf_link_init(uint8_t id);
void nrf_link_send(const lead_pkt_t *p);
uint8_t nrf_link_recv(lead_pkt_t *p);
#endif
