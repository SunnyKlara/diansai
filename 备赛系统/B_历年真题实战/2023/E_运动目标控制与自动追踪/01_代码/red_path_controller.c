/**
 * 2023E 红色光斑路径控制器
 * 
 * 【独到理解】
 * 这道题的红色系统看似简单（预设路径），实际暗藏陷阱：
 * 
 * 1. 角度→位置映射不是线性的！
 *    屏幕距离1m，光斑在边缘时，同样的角度变化对应更大的位移。
 *    需要用 x = D × tan(θ) 而不是 x = D × θ
 *    
 * 2. A4靶纸旋转后（要求4），不能简单地旋转坐标。
 *    因为靶纸贴在屏幕上的位置也变了，需要先检测靶纸位置再规划路径。
 *    但红色系统不能用摄像头（题目没说可以）！
 *    所以要么手动输入旋转角度，要么用另一种传感器检测。
 *    
 * 3. 沿胶带移动（要求3）的关键不是路径规划，而是速度控制。
 *    30秒走完A4纸周长约1m，平均速度约33mm/s。
 *    但舵机的角速度不均匀（边缘比中心快），需要补偿。
 * 
 * 【方案选择】
 * 用STM32控制两个舵机，预计算路径点序列，定时器中断逐点输出。
 * 不用OpenMV（红色系统是开环控制，不需要视觉反馈）。
 */

#include "stm32f1xx_hal.h"
#include <math.h>

#define PI 3.14159265f
#define SCREEN_DIST 1000.0f  // 屏幕距离 1000mm
#define SCREEN_HALF 250.0f   // 屏幕半边长 250mm (0.5m正方形)

/* ========== 坐标→舵机角度转换 ========== */

/**
 * 【关键函数】将屏幕上的坐标(mm)转换为舵机角度(度)
 * 
 * 这里有个容易犯的错误：很多人用线性映射 angle = x / D * 180/PI
 * 但正确的是 angle = atan(x / D) * 180/PI
 * 
 * 当x=250mm, D=1000mm时：
 *   线性近似: 250/1000 * 57.3 = 14.3°
 *   精确值:   atan(250/1000) * 57.3 = 14.0°
 *   差0.3°，对应屏幕上约5mm偏差——刚好超过2cm精度要求的边缘！
 * 
 * 所以必须用atan，不能用线性近似。
 */
typedef struct {
    float yaw_deg;    // 偏航角(度)，0=正前方
    float pitch_deg;  // 俯仰角(度)，0=水平
} GimbalAngle_t;

GimbalAngle_t xy_to_angle(float x_mm, float y_mm)
{
    GimbalAngle_t a;
    a.yaw_deg = atanf(x_mm / SCREEN_DIST) * 180.0f / PI;
    a.pitch_deg = atanf(y_mm / SCREEN_DIST) * 180.0f / PI;
    return a;
}

/* ========== 舵机角度→脉宽 ========== */

// 舵机标定：需要实测！不同舵机的角度-脉宽关系不同
// 典型值：0° → 500μs, 90° → 1500μs, 180° → 2500μs
// 但实际偏差可能有±50μs

// 标定参数（赛前用量角器标定）
static float yaw_us_per_deg = 11.1f;    // μs/度 (2000μs / 180°)
static float yaw_zero_us = 1500.0f;     // 0°对应的脉宽
static float pitch_us_per_deg = 11.1f;
static float pitch_zero_us = 1500.0f;

uint16_t angle_to_us(float angle_deg, float zero_us, float us_per_deg)
{
    float us = zero_us + angle_deg * us_per_deg;
    if (us < 500) us = 500;
    if (us > 2500) us = 2500;
    return (uint16_t)us;
}

/* ========== 路径生成器 ========== */

// 路径点序列
#define MAX_PATH_POINTS 600  // 30秒 × 20Hz = 600个点
static float path_x[MAX_PATH_POINTS];
static float path_y[MAX_PATH_POINTS];
static uint16_t path_len = 0;
static volatile uint16_t path_idx = 0;

/**
 * 生成正方形边线路径（要求2）
 * 
 * 【速度补偿的关键思考】
 * 正方形周长 = 4 × 500mm = 2000mm
 * 30秒走完，平均速度 = 66.7mm/s
 * 20Hz输出，每步 = 3.33mm
 * 
 * 但是！屏幕边缘的角速度和中心不同。
 * 如果用等角度步进，边缘的线速度会比中心快。
 * 如果用等距离步进（推荐），需要在角度域做非均匀采样。
 */
void generate_square_path(float half_size)
{
    // 等距离步进
    float perimeter = 4 * 2 * half_size;  // 周长
    float step = perimeter / MAX_PATH_POINTS;
    
    path_len = 0;
    float s = 0;  // 沿周长的累计距离
    
    for (int i = 0; i < MAX_PATH_POINTS && s < perimeter; i++) {
        float edge_len = 2 * half_size;
        
        if (s < edge_len) {
            // 上边：从(-half, +half)到(+half, +half)
            path_x[i] = -half_size + s;
            path_y[i] = half_size;
        } else if (s < 2 * edge_len) {
            // 右边：从(+half, +half)到(+half, -half)
            path_x[i] = half_size;
            path_y[i] = half_size - (s - edge_len);
        } else if (s < 3 * edge_len) {
            // 下边：从(+half, -half)到(-half, -half)
            path_x[i] = half_size - (s - 2 * edge_len);
            path_y[i] = -half_size;
        } else {
            // 左边：从(-half, -half)到(-half, +half)
            path_x[i] = -half_size;
            path_y[i] = -half_size + (s - 3 * edge_len);
        }
        
        s += step;
        path_len++;
    }
}

/**
 * 生成A4靶纸路径（要求3/4）
 * 
 * 【旋转处理的独到思路】
 * A4纸尺寸：297mm × 210mm
 * 胶带宽1.8cm，路径沿胶带中心线
 * 
 * 旋转角度rotation_deg由按键输入（因为红色系统没有摄像头）
 * 靶纸中心位置(cx, cy)也由按键输入
 * 
 * 旋转变换：
 *   x' = cx + (x-cx)×cos(θ) - (y-cy)×sin(θ)
 *   y' = cy + (x-cx)×sin(θ) + (y-cy)×cos(θ)
 */
void generate_a4_path(float cx, float cy, float rotation_deg)
{
    float half_w = 297.0f / 2;  // A4宽的一半
    float half_h = 210.0f / 2;  // A4高的一半
    float cos_r = cosf(rotation_deg * PI / 180.0f);
    float sin_r = sinf(rotation_deg * PI / 180.0f);
    
    // 先生成未旋转的A4矩形路径
    float raw_x[MAX_PATH_POINTS], raw_y[MAX_PATH_POINTS];
    float perimeter = 2 * (2*half_w + 2*half_h);
    float step = perimeter / MAX_PATH_POINTS;
    
    path_len = 0;
    float s = 0;
    
    for (int i = 0; i < MAX_PATH_POINTS && s < perimeter; i++) {
        float rx, ry;
        
        if (s < 2*half_w) {
            rx = -half_w + s; ry = half_h;
        } else if (s < 2*half_w + 2*half_h) {
            rx = half_w; ry = half_h - (s - 2*half_w);
        } else if (s < 4*half_w + 2*half_h) {
            rx = half_w - (s - 2*half_w - 2*half_h); ry = -half_h;
        } else {
            rx = -half_w; ry = -half_h + (s - 4*half_w - 2*half_h);
        }
        
        // 旋转变换
        path_x[i] = cx + rx * cos_r - ry * sin_r;
        path_y[i] = cy + rx * sin_r + ry * cos_r;
        
        s += step;
        path_len++;
    }
}

/* ========== 定时器中断：20Hz输出路径点 ========== */

extern TIM_HandleTypeDef htim2;  // 舵机PWM
extern TIM_HandleTypeDef htim3;  // 路径输出定时器(20Hz)

void path_output_tick(void)
{
    if (path_idx >= path_len) return;
    
    GimbalAngle_t a = xy_to_angle(path_x[path_idx], path_y[path_idx]);
    
    uint16_t yaw_us = angle_to_us(a.yaw_deg, yaw_zero_us, yaw_us_per_deg);
    uint16_t pitch_us = angle_to_us(a.pitch_deg, pitch_zero_us, pitch_us_per_deg);
    
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, yaw_us);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, pitch_us);
    
    path_idx++;
}

/* ========== 复位到原点（要求1） ========== */

/**
 * 【复位策略】
 * 不能直接跳到原点（舵机会猛转，激光笔甩出屏幕）
 * 应该平滑移动到原点，用S曲线或线性插值
 */
void move_to_origin(void)
{
    // 当前位置
    float cur_yaw = 0, cur_pitch = 0;  // 从当前舵机脉宽反算
    
    // 目标：原点(0,0) → 角度(0,0)
    // 分50步平滑移动（50步 × 20ms = 1秒）
    for (int i = 1; i <= 50; i++) {
        float t = (float)i / 50.0f;
        // S曲线插值：t' = 3t² - 2t³
        float s = 3*t*t - 2*t*t*t;
        
        float yaw = cur_yaw * (1-s);
        float pitch = cur_pitch * (1-s);
        
        uint16_t yaw_us_val = angle_to_us(yaw, yaw_zero_us, yaw_us_per_deg);
        uint16_t pitch_us_val = angle_to_us(pitch, pitch_zero_us, pitch_us_per_deg);
        
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, yaw_us_val);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, pitch_us_val);
        
        HAL_Delay(20);
    }
}
