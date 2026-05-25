/**
 * 2024E 三子棋游戏装置 - 主程序
 * MCU: STM32F103C8T6
 * 
 * 系统组成：
 *   - OpenMV: 俯拍棋盘，识别棋子位置和颜色
 *   - STM32: 博弈算法 + XY滑台控制 + 人机交互
 *   - XY滑台: 步进电机×2 + 舵机Z轴 + 电磁铁
 * 
 * 工作流程：
 *   1. OpenMV扫描棋盘 → 发送棋盘状态给STM32
 *   2. STM32运行Minimax算法 → 决定落子位置
 *   3. STM32控制XY滑台拾取棋子并放置
 *   4. 亮灯提示人下棋
 *   5. 人下完按按钮 → 回到步骤1
 */

#include "stm32f1xx_hal.h"
#include "tictactoe_ai.h"
#include "xy_gantry.h"
#include <stdio.h>
#include <string.h>

/* ========== 全局变量 ========== */
static int8_t board[9] = {0};           // 当前棋盘
static int8_t prev_board[9] = {0};      // 上一次棋盘（检测移动用）
static uint8_t self_piece_count = 0;    // 己方已用棋子数
static uint8_t human_piece_count = 0;   // 对方已用棋子数
static uint8_t game_over = 0;

/* ========== OpenMV通信 ========== */
// OpenMV发送格式: "$b0,b1,b2,b3,b4,b5,b6,b7,b8\n"
// bi: 0=空, 1=黑棋, 2=白棋

static uint8_t uart_buf[64];
static uint8_t uart_idx = 0;

void parse_board_from_openmv(char* data)
{
    if (data[0] != '$') return;
    
    int vals[9];
    if (sscanf(data, "$%d,%d,%d,%d,%d,%d,%d,%d,%d",
               &vals[0],&vals[1],&vals[2],
               &vals[3],&vals[4],&vals[5],
               &vals[6],&vals[7],&vals[8]) == 9) {
        for (int i = 0; i < 9; i++) {
            if (vals[i] == 0) board[i] = EMPTY;
            else if (vals[i] == 1) board[i] = SELF;   // 黑=己方
            else if (vals[i] == 2) board[i] = HUMAN;  // 白=对方
        }
    }
}

void request_board_scan(void)
{
    // 发送扫描命令给OpenMV
    uint8_t cmd[] = "SCAN\n";
    HAL_UART_Transmit(&huart1, cmd, 5, 100);
    
    // 等待接收棋盘数据
    HAL_Delay(500);  // OpenMV处理时间
    
    // 接收数据（中断方式，数据在uart_buf中）
    parse_board_from_openmv((char*)uart_buf);
}

/* ========== LED指示 ========== */
#define LED_RED_PORT   GPIOB
#define LED_RED_PIN    GPIO_PIN_5
#define LED_GREEN_PORT GPIOB
#define LED_GREEN_PIN  GPIO_PIN_4

void led_my_turn(void)
{
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
}

void led_your_turn(void)
{
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
}

void led_game_over(void)
{
    // 交替闪烁
    for (int i = 0; i < 10; i++) {
        HAL_GPIO_TogglePin(LED_RED_PORT, LED_RED_PIN);
        HAL_GPIO_TogglePin(LED_GREEN_PORT, LED_GREEN_PIN);
        HAL_Delay(300);
    }
}

/* ========== 主程序 ========== */
extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart1;

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM2_Init();     // 舵机PWM
    MX_USART1_Init();   // OpenMV通信
    // 步进电机用GPIO直接控制，不需要定时器
    
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);  // Z轴舵机
    
    gantry_init();
    
    // 清空棋盘
    memset(board, 0, sizeof(board));
    memset(prev_board, 0, sizeof(prev_board));
    self_piece_count = 0;
    human_piece_count = 0;
    game_over = 0;
    
    // 等待启动按键（PC13）
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        HAL_Delay(10);
    }
    HAL_Delay(200);
    
    /* ========== 游戏主循环 ========== */
    // 要求(4): 装置执黑先行
    // 要求(5): 人执黑先行 → 通过按键选择模式
    
    uint8_t self_first = 1;  // 1=装置先手, 0=人先手
    // 可以通过另一个按键选择
    
    while (!game_over) {
        
        if (self_first || self_piece_count > 0) {
            // ===== 装置回合 =====
            led_my_turn();
            
            // 保存当前棋盘（用于检测移动）
            memcpy(prev_board, board, sizeof(board));
            
            // AI计算最佳落子
            int best_move = find_best_move(board);
            
            if (best_move >= 0) {
                // 拾取棋子并放置
                pick_and_place(0, self_piece_count, best_move);  // 0=黑棋
                board[best_move] = SELF;
                self_piece_count++;
            }
            
            // 检查胜负
            int winner = check_winner(board);
            if (winner == SELF) {
                led_game_over();
                game_over = 1;
                break;
            }
            if (is_full(board)) {
                led_game_over();  // 平局
                game_over = 1;
                break;
            }
        }
        
        self_first = 1;  // 后续都是交替下
        
        // ===== 人的回合 =====
        led_your_turn();  // 绿灯亮，提示人下棋
        
        // 等待人按按钮（表示下完了）
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
            HAL_Delay(10);
        }
        HAL_Delay(200);
        
        // 扫描棋盘，获取人下的棋
        request_board_scan();
        human_piece_count++;
        
        // 要求(6): 检测棋子是否被移动
        int moved_from, moved_to;
        if (detect_moved_piece(prev_board, board, &moved_from, &moved_to)) {
            // 棋子被移动了！放回原位
            // moved_to是棋子应该在的位置
            pick_and_place(0, self_piece_count, moved_to);  // 放回
            // 重新扫描
            request_board_scan();
        }
        
        // 检查胜负
        int winner = check_winner(board);
        if (winner == HUMAN) {
            led_game_over();
            game_over = 1;
            break;
        }
        if (is_full(board)) {
            led_game_over();
            game_over = 1;
            break;
        }
    }
    
    // 游戏结束，回原点
    gantry_home();
    
    while (1) {
        HAL_Delay(1000);
    }
}
