/**
 * 2024E 三子棋博弈算法
 * 
 * 三子棋是已解博弈：
 *   - 先手最优策略：必不败（赢或平）
 *   - 后手最优策略：必不败（平）
 * 
 * 算法：Minimax + Alpha-Beta剪枝
 * 三子棋状态空间极小（最多9!≈362880种），穷举无压力
 * 
 * 棋盘表示：
 *   board[9]，索引对应方格编号：
 *   [0][1][2]     [1][2][3]
 *   [3][4][5]  =  [4][5][6]  (题目编号)
 *   [6][7][8]     [7][8][9]
 *   
 *   0=空, 1=己方(装置), -1=对方(人)
 */

#include <stdint.h>
#include <string.h>

#define EMPTY   0
#define SELF    1    // 装置方
#define HUMAN  -1    // 人方

/* ========== 胜负判断 ========== */

// 所有获胜组合（8种）
static const uint8_t win_lines[8][3] = {
    {0,1,2}, {3,4,5}, {6,7,8},  // 横
    {0,3,6}, {1,4,7}, {2,5,8},  // 竖
    {0,4,8}, {2,4,6}             // 斜
};

/**
 * 检查是否有人获胜
 * @return 1=SELF赢, -1=HUMAN赢, 0=未结束或平局
 */
int check_winner(int8_t board[9])
{
    for (int i = 0; i < 8; i++) {
        int sum = board[win_lines[i][0]] + board[win_lines[i][1]] + board[win_lines[i][2]];
        if (sum == 3) return SELF;
        if (sum == -3) return HUMAN;
    }
    return 0;
}

/**
 * 检查棋盘是否已满
 */
int is_full(int8_t board[9])
{
    for (int i = 0; i < 9; i++) {
        if (board[i] == EMPTY) return 0;
    }
    return 1;
}

/* ========== Minimax算法 ========== */

/**
 * Minimax递归搜索
 * @param board     当前棋盘
 * @param depth     搜索深度
 * @param is_max    是否是己方回合(最大化)
 * @param alpha     Alpha剪枝
 * @param beta      Beta剪枝
 * @return 评估分数
 */
int minimax(int8_t board[9], int depth, int is_max, int alpha, int beta)
{
    int winner = check_winner(board);
    if (winner == SELF)  return 10 - depth;   // 己方赢，越早赢分越高
    if (winner == HUMAN) return depth - 10;   // 对方赢，越早输分越低
    if (is_full(board))  return 0;             // 平局
    
    if (is_max) {
        int best = -100;
        for (int i = 0; i < 9; i++) {
            if (board[i] == EMPTY) {
                board[i] = SELF;
                int score = minimax(board, depth + 1, 0, alpha, beta);
                board[i] = EMPTY;
                if (score > best) best = score;
                if (best > alpha) alpha = best;
                if (beta <= alpha) break;  // Beta剪枝
            }
        }
        return best;
    } else {
        int best = 100;
        for (int i = 0; i < 9; i++) {
            if (board[i] == EMPTY) {
                board[i] = HUMAN;
                int score = minimax(board, depth + 1, 1, alpha, beta);
                board[i] = EMPTY;
                if (score < best) best = score;
                if (best < beta) beta = best;
                if (beta <= alpha) break;  // Alpha剪枝
            }
        }
        return best;
    }
}

/**
 * 找最佳落子位置
 * @param board  当前棋盘状态
 * @return 最佳落子位置(0~8)，-1表示无法落子
 */
int find_best_move(int8_t board[9])
{
    int best_score = -100;
    int best_move = -1;
    
    for (int i = 0; i < 9; i++) {
        if (board[i] == EMPTY) {
            board[i] = SELF;
            int score = minimax(board, 0, 0, -100, 100);
            board[i] = EMPTY;
            
            if (score > best_score) {
                best_score = score;
                best_move = i;
            }
        }
    }
    
    return best_move;
}

/* ========== 棋盘变化检测（要求6：检测棋子被移动） ========== */

/**
 * 比较两个棋盘状态，找出被移动的棋子
 * @param prev  上一次的棋盘状态
 * @param curr  当前棋盘状态
 * @param moved_from  被移走的位置（输出）
 * @param moved_to    被移到的位置（输出）
 * @return 1=检测到移动, 0=无变化
 */
int detect_moved_piece(int8_t prev[9], int8_t curr[9], 
                        int* moved_from, int* moved_to)
{
    int disappeared = -1;  // 原来有棋子现在没了
    int appeared = -1;     // 原来没棋子现在有了
    
    for (int i = 0; i < 9; i++) {
        if (prev[i] == SELF && curr[i] == EMPTY) {
            disappeared = i;  // 己方棋子消失了
        }
        if (prev[i] == EMPTY && curr[i] == SELF) {
            appeared = i;     // 己方棋子出现在新位置
        }
    }
    
    if (disappeared >= 0) {
        *moved_from = appeared;  // 棋子被移到的位置
        *moved_to = disappeared; // 棋子原来的位置（需要放回）
        return 1;
    }
    
    return 0;
}

/* ========== 测试用：打印棋盘 ========== */
#ifdef DEBUG_PRINT
#include <stdio.h>
void print_board(int8_t board[9])
{
    const char symbols[] = {'.', 'X', ' ', 'O'};  // 0=., 1=X, -1=O
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int idx = r * 3 + c;
            char ch = (board[idx] == SELF) ? 'X' : (board[idx] == HUMAN) ? 'O' : '.';
            printf(" %c ", ch);
            if (c < 2) printf("|");
        }
        printf("\n");
        if (r < 2) printf("---+---+---\n");
    }
    printf("\n");
}
#endif
