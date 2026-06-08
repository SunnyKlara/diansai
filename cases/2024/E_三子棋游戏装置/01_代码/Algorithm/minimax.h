/**
 * @file    minimax.h
 * @brief   井字棋 Minimax + α-β 剪枝
 *          board[9]：0=空, 1=装置, 2=对手
 */
#ifndef __MINIMAX_H__
#define __MINIMAX_H__
#include <stdint.h>

typedef enum { MM_RUNNING, MM_WIN_DEVICE, MM_WIN_HUMAN, MM_DRAW } minimax_state_t;

int8_t minimax_best_move(const uint8_t board[9]);   /* 返回 0..8，无可走返回 -1 */
minimax_state_t minimax_check(const uint8_t board[9]);

#endif
