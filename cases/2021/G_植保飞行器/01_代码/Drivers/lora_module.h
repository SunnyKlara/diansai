#ifndef __LORA_MODULE_H__
#define __LORA_MODULE_H__
#include <stdint.h>
void lora_init(void);
void lora_send(const uint8_t *data, uint8_t len);
uint8_t lora_recv(uint8_t *out, uint8_t max);
#endif
