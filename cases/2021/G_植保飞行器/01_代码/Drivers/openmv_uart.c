#include "openmv_uart.h"
#include "../Algorithm/spray_planner.h"

static volatile uint8_t s_state = 0;
static volatile uint8_t s_buf[7];
static volatile uint8_t s_idx = 0;
static volatile target_t s_t;
static volatile uint8_t s_new = 0;

void omv_init(void){ s_state = 0; s_idx = 0; s_new = 0; }

void omv_on_byte(uint8_t b)
{
    if (s_state == 0) { if (b == 0xAA) { s_state = 1; s_idx = 0; } return; }
    s_buf[s_idx++] = b;
    if (s_idx == 6) {
        uint8_t crc = s_buf[0] ^ s_buf[1] ^ s_buf[2] ^ s_buf[3] ^ s_buf[4];
        if (crc == s_buf[5]) {
            s_t.color = s_buf[0];
            s_t.x = (float)((int16_t)((s_buf[1] << 8) | s_buf[2])) * 0.01f;
            s_t.y = (float)((int16_t)((s_buf[3] << 8) | s_buf[4])) * 0.01f;
            s_new = 1;
        }
        s_state = 0; s_idx = 0;
    }
}

uint8_t omv_get_target(target_t *t)
{
    if (!s_new || !t) return 0;
    *t = s_t; s_new = 0; return 1;
}
