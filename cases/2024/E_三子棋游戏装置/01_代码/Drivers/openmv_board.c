#include "openmv_board.h"
static volatile uint8_t s_board[9] = {0};
static volatile uint8_t s_state = 0;
static volatile uint8_t s_idx = 0;
static volatile uint8_t s_buf[10];
static volatile uint8_t s_new = 0;

void omv_init(void){ s_state = 0; s_idx = 0; s_new = 0; }

void omv_on_byte(uint8_t b)
{
    if (s_state == 0) { if (b == 0xAA) { s_state = 1; s_idx = 0; } return; }
    s_buf[s_idx++] = b;
    if (s_idx == 10) {
        uint8_t crc = 0;
        for (uint8_t i = 0; i < 9; i++) crc ^= s_buf[i];
        if (crc == s_buf[9]) {
            for (uint8_t i = 0; i < 9; i++) s_board[i] = s_buf[i];
            s_new = 1;
        }
        s_state = 0; s_idx = 0;
    }
}

uint8_t omv_get_board(uint8_t board[9])
{
    if (!s_new || !board) return 0;
    for (uint8_t i = 0; i < 9; i++) board[i] = s_board[i];
    s_new = 0;
    return 1;
}
