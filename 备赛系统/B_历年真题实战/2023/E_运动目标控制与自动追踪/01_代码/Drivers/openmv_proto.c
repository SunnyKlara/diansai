/**
 * @file    openmv_proto.c
 * @brief   接收 OpenMV 红色光斑位置：协议 [0xAA][cx hi][cx lo][cy hi][cy lo][crc]
 */
#include "openmv_proto.h"

static volatile uint8_t s_state = 0;
static volatile uint8_t s_buf[6];
static volatile uint8_t s_idx = 0;
static volatile int16_t s_cx = 0, s_cy = 0;
static volatile uint8_t s_new = 0;

void omv_init(void) { s_state = 0; s_idx = 0; s_new = 0; }

void omv_on_byte(uint8_t b)
{
    if (s_state == 0) { if (b == 0xAA) { s_state = 1; s_idx = 0; } return; }
    s_buf[s_idx++] = b;
    if (s_idx == 5) {
        uint8_t crc = s_buf[0] ^ s_buf[1] ^ s_buf[2] ^ s_buf[3];
        if (crc == s_buf[4]) {
            s_cx = (int16_t)((s_buf[0] << 8) | s_buf[1]);
            s_cy = (int16_t)((s_buf[2] << 8) | s_buf[3]);
            s_new = 1;
        }
        s_state = 0; s_idx = 0;
    }
}

uint8_t omv_get(int16_t *cx, int16_t *cy)
{
    if (!s_new || !cx || !cy) return 0;
    *cx = s_cx; *cy = s_cy; s_new = 0;
    return 1;
}
