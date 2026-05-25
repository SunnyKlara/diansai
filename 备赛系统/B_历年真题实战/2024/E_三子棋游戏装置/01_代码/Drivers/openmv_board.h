#ifndef __OPENMV_BOARD_H__
#define __OPENMV_BOARD_H__
#include <stdint.h>
void omv_init(void);
void omv_on_byte(uint8_t b);
uint8_t omv_get_board(uint8_t board[9]);
#endif
