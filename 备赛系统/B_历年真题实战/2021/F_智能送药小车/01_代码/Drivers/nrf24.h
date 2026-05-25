#ifndef __NRF24_H__
#define __NRF24_H__
#include <stdint.h>

typedef struct {
    uint8_t car_id;
    uint8_t target_room;
    uint8_t at_node;
    uint8_t status;          /* 0=idle, 1=busy, 2=arrived */
} nrf_msg_t;

void nrf24_init(uint8_t car_id);
void nrf24_send(const nrf_msg_t *msg);
uint8_t nrf24_recv(nrf_msg_t *msg);

#endif
