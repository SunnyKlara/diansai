/**
 * @file    minimax.c
 * @brief   井字棋 Minimax AI（α-β 剪枝）
 *
 * 棋盘 3×3，每格 0=空 1=装置 2=对手
 * 完整搜索 9 层 → 装置必胜或平局
 */
#include "minimax.h"
#include <stdint.h>

#define INF 1000

static int8_t check_win(const uint8_t *b)
{
    /* 8 个胜利组合 */
    static const uint8_t LINES[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t a = b[LINES[i][0]];
        if (a && b[LINES[i][1]] == a && b[LINES[i][2]] == a) return (int8_t)a;
    }
    return 0;
}

static uint8_t board_full(const uint8_t *b)
{
    for (uint8_t i = 0; i < 9; i++) if (!b[i]) return 0;
    return 1;
}

static int score_board(const uint8_t *b, uint8_t depth)
{
    int8_t w = check_win(b);
    if (w == 1) return 10 - (int)depth;
    if (w == 2) return -10 + (int)depth;
    return 0;
}

static int minimax_rec(uint8_t *b, uint8_t depth, uint8_t maximizing,
                       int alpha, int beta)
{
    int8_t w = check_win(b);
    if (w || board_full(b)) return score_board(b, depth);

    if (maximizing) {
        int best = -INF;
        for (uint8_t i = 0; i < 9; i++) {
            if (!b[i]) {
                b[i] = 1;
                int v = minimax_rec(b, depth + 1, 0, alpha, beta);
                b[i] = 0;
                if (v > best) best = v;
                if (best > alpha) alpha = best;
                if (beta <= alpha) break;
            }
        }
        return best;
    } else {
        int best = INF;
        for (uint8_t i = 0; i < 9; i++) {
            if (!b[i]) {
                b[i] = 2;
                int v = minimax_rec(b, depth + 1, 1, alpha, beta);
                b[i] = 0;
                if (v < best) best = v;
                if (best < beta) beta = best;
                if (beta <= alpha) break;
            }
        }
        return best;
    }
}

int8_t minimax_best_move(const uint8_t *b)
{
    uint8_t board[9];
    for (uint8_t i = 0; i < 9; i++) board[i] = b[i];

    int8_t best_move = -1;
    int best_v = -INF;
    for (uint8_t i = 0; i < 9; i++) {
        if (!board[i]) {
            board[i] = 1;
            int v = minimax_rec(board, 1, 0, -INF, INF);
            board[i] = 0;
            if (v > best_v) { best_v = v; best_move = (int8_t)i; }
        }
    }
    return best_move;
}

minimax_state_t minimax_check(const uint8_t *b)
{
    int8_t w = check_win(b);
    if (w == 1) return MM_WIN_DEVICE;
    if (w == 2) return MM_WIN_HUMAN;
    if (board_full(b)) return MM_DRAW;
    return MM_RUNNING;
}
