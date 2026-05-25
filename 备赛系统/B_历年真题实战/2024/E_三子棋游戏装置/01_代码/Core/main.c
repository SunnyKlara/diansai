/**
 * @file    main.c
 * @brief   2024 E 三子棋游戏装置 主程序
 */
#include "../config.h"
#include "../Drivers/xy_gantry.h"
#include "../Drivers/openmv_board.h"
#include "../Algorithm/minimax.h"

typedef enum { ST_INIT, ST_WAIT_HUMAN, ST_DEVICE_THINK, ST_DEVICE_MOVE, ST_END } st_t;

static volatile st_t s_state = ST_INIT;
static volatile uint32_t s_tick_ms = 0;
static uint8_t s_board[9] = {0};

void main_systick_isr(void){ s_tick_ms++; }

static void place_piece_at(uint8_t cell)
{
    int32_t x = ((int32_t)(cell % 3)) * BOARD_CELL_MM;
    int32_t y = ((int32_t)(cell / 3)) * BOARD_CELL_MM;
    /* 1. 移到棋盒，吸子 */
    xy_move_to(BOX_X_MM, BOX_Y_MM);
    xy_pickup(1);
    /* 2. 移到目标格，松手 */
    xy_move_to(x, y);
    xy_pickup(0);
}

int main(void)
{
    xy_init(); xy_home();
    omv_init();

    s_state = ST_WAIT_HUMAN;

    while (1) {
        switch (s_state) {
            case ST_WAIT_HUMAN:
                if (omv_get_board(s_board)) {
                    minimax_state_t st = minimax_check(s_board);
                    if (st != MM_RUNNING) { s_state = ST_END; break; }
                    s_state = ST_DEVICE_THINK;
                }
                break;
            case ST_DEVICE_THINK: {
                int8_t mv = minimax_best_move(s_board);
                if (mv >= 0) {
                    s_board[mv] = 1;
                    place_piece_at((uint8_t)mv);
                    s_state = ST_WAIT_HUMAN;
                } else {
                    s_state = ST_END;
                }
                break;
            }
            default: break;
        }
    }
}
