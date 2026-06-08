#include "lora_module.h"
void lora_init(void){}
void lora_send(const uint8_t *data, uint8_t len){ (void)data; (void)len; }
uint8_t lora_recv(uint8_t *out, uint8_t max){ (void)out; (void)max; return 0; }
