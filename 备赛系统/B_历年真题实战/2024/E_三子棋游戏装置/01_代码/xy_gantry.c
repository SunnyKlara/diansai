/**
 * 2024E XY滑台控制模块
 * 
 * 机械结构：
 *   X轴：步进电机+同步带，行程约200mm
 *   Y轴：步进电机+同步带，行程约200mm
 *   Z轴：舵机升降，行程约30mm
 *   末端：电磁铁吸取棋子
 * 
 * 坐标系：
 *   原点(0,0)在棋盘左上角(1号方格中心)
 *   X轴向右，Y轴向下
 *   方格间距30mm
 *   
 *   方格中心坐标：
 *   格1(0,0)   格2(30,0)   格3(60,0)
 *   格4(0,30)  格5(30,30)  格6(60,30)
 *   格7(0,60)  格8(30,60)  格9(60,60)
 * 
 * 棋子放置处坐标：
 *   黑棋：X=-40mm, Y=0~60mm (棋盘左侧)
 *   白棋：X=100mm, Y=0~60mm (棋盘右侧)
 */

#include "stm32f1xx_hal.h"

/* ========== 步进电机参数 ========== */
#define STEPS_PER_REV   200     // 步进电机每转步数(1.8°)
#define MICROSTEP       16      // 微步细分
#define BELT_PITCH      2.0f    // GT2同步带齿距 2mm
#define PULLEY_TEETH    20      // 同步轮齿数
#define MM_PER_STEP     (BELT_PITCH * PULLEY_TEETH / (STEPS_PER_REV * MICROSTEP))
// = 2 * 20 / (200 * 16) = 0.0125mm/步 → 精度足够

/* ========== 引脚定义 ========== */
// X轴步进电机
#define X_STEP_PORT  GPIOB
#define X_STEP_PIN   GPIO_PIN_12
#define X_DIR_PORT   GPIOB
#define X_DIR_PIN    GPIO_PIN_13

// Y轴步进电机
#define Y_STEP_PORT  GPIOB
#define Y_STEP_PIN   GPIO_PIN_14
#define Y_DIR_PORT   GPIOB
#define Y_DIR_PIN    GPIO_PIN_15

// Z轴舵机
// PA1 TIM2_CH2 (50Hz PWM)

// 电磁铁
#define MAGNET_PORT  GPIOA
#define MAGNET_PIN   GPIO_PIN_4

/* ========== 当前位置 ========== */
static float current_x = 0;  // mm
static float current_y = 0;  // mm

/* ========== 步进电机控制 ========== */

static void step_pulse(GPIO_TypeDef* port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    // 延时约5μs（步进电机最小脉冲宽度）
    for (volatile int i = 0; i < 50; i++);
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    for (volatile int i = 0; i < 50; i++);
}

/**
 * 移动X轴
 * @param distance_mm 移动距离(mm)，正=向右，负=向左
 * @param speed_delay 步间延时(μs)，越小越快
 */
void move_x(float distance_mm, uint16_t speed_delay_us)
{
    // 方向
    if (distance_mm > 0) {
        HAL_GPIO_WritePin(X_DIR_PORT, X_DIR_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(X_DIR_PORT, X_DIR_PIN, GPIO_PIN_RESET);
        distance_mm = -distance_mm;
    }
    
    // 步数
    uint32_t steps = (uint32_t)(distance_mm / MM_PER_STEP + 0.5f);
    
    // 执行
    for (uint32_t i = 0; i < steps; i++) {
        step_pulse(X_STEP_PORT, X_STEP_PIN);
        // 延时控制速度
        for (volatile uint32_t d = 0; d < speed_delay_us * 10; d++);
    }
}

void move_y(float distance_mm, uint16_t speed_delay_us)
{
    if (distance_mm > 0) {
        HAL_GPIO_WritePin(Y_DIR_PORT, Y_DIR_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(Y_DIR_PORT, Y_DIR_PIN, GPIO_PIN_RESET);
        distance_mm = -distance_mm;
    }
    
    uint32_t steps = (uint32_t)(distance_mm / MM_PER_STEP + 0.5f);
    
    for (uint32_t i = 0; i < steps; i++) {
        step_pulse(Y_STEP_PORT, Y_STEP_PIN);
        for (volatile uint32_t d = 0; d < speed_delay_us * 10; d++);
    }
}

/* ========== Z轴舵机 ========== */
extern TIM_HandleTypeDef htim2;

void z_up(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 1000);  // 抬起
    HAL_Delay(300);
}

void z_down(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 1800);  // 放下
    HAL_Delay(300);
}

/* ========== 电磁铁 ========== */

void magnet_on(void)
{
    HAL_GPIO_WritePin(MAGNET_PORT, MAGNET_PIN, GPIO_PIN_SET);
    HAL_Delay(100);  // 等磁力建立
}

void magnet_off(void)
{
    HAL_GPIO_WritePin(MAGNET_PORT, MAGNET_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
}

/* ========== 高级接口 ========== */

/**
 * 移动到绝对坐标
 */
void gantry_move_to(float target_x, float target_y)
{
    float dx = target_x - current_x;
    float dy = target_y - current_y;
    
    // 先抬起Z轴
    z_up();
    
    // 移动XY
    move_x(dx, 100);
    move_y(dy, 100);
    
    current_x = target_x;
    current_y = target_y;
}

/**
 * 获取方格中心坐标
 * @param grid_num 方格编号(0~8)
 */
void get_grid_pos(uint8_t grid_num, float* x, float* y)
{
    *x = (grid_num % 3) * 30.0f;
    *y = (grid_num / 3) * 30.0f;
}

/**
 * 从棋子放置处拾取棋子
 * @param color 0=黑棋(左侧), 1=白棋(右侧)
 * @param index 第几颗(0~4)
 */
void pick_piece(uint8_t color, uint8_t index)
{
    float px, py;
    if (color == 0) {
        px = -40.0f;  // 黑棋在左侧
    } else {
        px = 100.0f;  // 白棋在右侧
    }
    py = index * 15.0f;  // 棋子间距15mm
    
    gantry_move_to(px, py);
    z_down();
    magnet_on();
    z_up();
}

/**
 * 放置棋子到指定方格
 * @param grid_num 方格编号(0~8)
 */
void place_piece(uint8_t grid_num)
{
    float gx, gy;
    get_grid_pos(grid_num, &gx, &gy);
    
    gantry_move_to(gx, gy);
    z_down();
    magnet_off();
    z_up();
}

/**
 * 完整的拾取-放置动作
 * @param color    棋子颜色(0=黑, 1=白)
 * @param piece_idx 第几颗棋子
 * @param grid_num  目标方格(0~8)
 */
void pick_and_place(uint8_t color, uint8_t piece_idx, uint8_t grid_num)
{
    pick_piece(color, piece_idx);
    place_piece(grid_num);
}

/**
 * 回到原点
 */
void gantry_home(void)
{
    gantry_move_to(0, 0);
}

/**
 * 初始化（回原点）
 */
void gantry_init(void)
{
    current_x = 0;
    current_y = 0;
    z_up();
    magnet_off();
    
    // 如果有限位开关，这里执行回零动作
    // 简化版：假设上电时在原点
}
