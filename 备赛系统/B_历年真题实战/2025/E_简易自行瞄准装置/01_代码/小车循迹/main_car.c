/**
 * 2025E 小车循迹控制 - MSPM0G3507
 * 
 * 功能：沿100cm正方形黑线轨迹循迹行驶
 * 传感器：5路红外对管阵列
 * 驱动：TB6612双路电机驱动
 * 测速：AB相编码器×2
 * 
 * 引脚分配(MSPM0G3507):
 *   PA12~PA16  -> 5路红外传感器输入 (GPIO)
 *   PA0/PA1    -> 电机A PWM (TIMA0_C0/C1)
 *   PA2/PA3    -> 电机B PWM (TIMA0_C2/C3)
 *   PB0/PB1    -> 编码器A (TIMG0)
 *   PB2/PB3    -> 编码器B (TIMG1)
 *   PA6        -> 蜂鸣器
 *   PA7        -> LED指示灯
 */

#include "ti_msp_dl_config.h"
#include <math.h>

/* ========== 参数定义 ========== */
#define TRACK_WIDTH     18      // 黑线宽度 1.8cm
#define SQUARE_SIDE     1000    // 正方形边长 1000mm
#define WHEEL_DIAMETER  65      // 轮子直径 mm
#define ENCODER_PPR     330     // 编码器每转脉冲数
#define WHEEL_CIRCUMF   (3.14159f * WHEEL_DIAMETER)  // 轮子周长 mm

// PID参数 - 循迹
#define KP_TRACK    25.0f
#define KI_TRACK    0.1f
#define KD_TRACK    15.0f

// PID参数 - 速度
#define KP_SPEED    2.0f
#define KI_SPEED    0.5f

// 速度设定
#define BASE_SPEED  60      // 基础PWM占空比 (0~100)
#define MAX_SPEED   90

/* ========== 全局变量 ========== */
typedef struct {
    int32_t left_count;     // 左轮编码器累计
    int32_t right_count;    // 右轮编码器累计
    float x_mm;             // 估算X坐标 (mm)
    float y_mm;             // 估算Y坐标 (mm)
    float heading_deg;      // 估算航向角 (度)
    float distance_mm;      // 累计行驶距离 (mm)
} Odometry_t;

static Odometry_t odom = {0};
static int target_laps = 1;
static int current_lap = 0;
static int corner_count = 0;  // 转弯计数（4个弯=1圈）

/* ========== 红外传感器读取 ========== */
// 5路传感器排列: [L2 L1 C R1 R2]
// 在黑线上返回1，白色返回0
typedef struct {
    uint8_t L2, L1, C, R1, R2;
} SensorState_t;

SensorState_t read_sensors(void) {
    SensorState_t s;
    s.L2 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_12) ? 0 : 1;
    s.L1 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_13) ? 0 : 1;
    s.C  = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_14) ? 0 : 1;
    s.R1 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_15) ? 0 : 1;
    s.R2 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_16) ? 0 : 1;
    return s;
}

// 将传感器状态转换为偏差值 (-2 ~ +2)
float sensor_to_error(SensorState_t s) {
    // 加权平均法
    float weighted = -2.0f*s.L2 + -1.0f*s.L1 + 0.0f*s.C + 1.0f*s.R1 + 2.0f*s.R2;
    float sum = s.L2 + s.L1 + s.C + s.R1 + s.R2;
    
    if (sum == 0) return 0;  // 全白：保持上次方向
    return weighted / sum;
}

/* ========== 电机控制 ========== */
void motor_set(int left_speed, int right_speed) {
    // left_speed/right_speed: -100 ~ +100
    // 正值前进，负值后退
    
    if (left_speed >= 0) {
        // 左轮正转
        DL_TimerA_setCaptureCompareValue(TIMA0, left_speed * 10, DL_TIMER_CC_0_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_1_INDEX);
    } else {
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_0_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, (-left_speed) * 10, DL_TIMER_CC_1_INDEX);
    }
    
    if (right_speed >= 0) {
        DL_TimerA_setCaptureCompareValue(TIMA0, right_speed * 10, DL_TIMER_CC_2_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_3_INDEX);
    } else {
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_2_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, (-right_speed) * 10, DL_TIMER_CC_3_INDEX);
    }
}

/* ========== PID循迹控制 ========== */
static float prev_error = 0;
static float integral = 0;

float track_pid(float error) {
    integral += error;
    if (integral > 100) integral = 100;
    if (integral < -100) integral = -100;
    
    float derivative = error - prev_error;
    prev_error = error;
    
    return KP_TRACK * error + KI_TRACK * integral + KD_TRACK * derivative;
}

/* ========== 转弯检测 ========== */
// 正方形轨迹的90度弯通过传感器全黑或特殊模式检测
uint8_t detect_corner(SensorState_t s) {
    // 全部传感器都在黑线上 = 到达T字路口或十字路口
    // 正方形轨迹的弯道处黑线会有垂直交叉
    return (s.L2 && s.L1 && s.C && s.R1 && s.R2);
}

/* ========== 主程序 ========== */
int main(void) {
    SYSCFG_DL_init();  // SysConfig生成的初始化
    
    // 等待启动按键
    while (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_8)) {
        delay_cycles(1000);
    }
    delay_cycles(16000000);  // 消抖 1s
    
    // 蜂鸣器提示启动
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_6);
    delay_cycles(8000000);  // 0.5s
    DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_6);
    
    // 主循环：循迹行驶
    while (current_lap < target_laps) {
        SensorState_t sensors = read_sensors();
        float error = sensor_to_error(sensors);
        float correction = track_pid(error);
        
        int left  = BASE_SPEED - (int)correction;
        int right = BASE_SPEED + (int)correction;
        
        // 限幅
        if (left > MAX_SPEED) left = MAX_SPEED;
        if (left < -MAX_SPEED) left = -MAX_SPEED;
        if (right > MAX_SPEED) right = MAX_SPEED;
        if (right < -MAX_SPEED) right = -MAX_SPEED;
        
        motor_set(left, right);
        
        // 转弯检测
        if (detect_corner(sensors)) {
            corner_count++;
            if (corner_count % 4 == 0) {
                current_lap++;
            }
            // 通过弯道后短暂延时避免重复检测
            delay_cycles(8000000);  // 0.5s
        }
        
        delay_cycles(160000);  // 10ms控制周期
    }
    
    // 停车
    motor_set(0, 0);
    
    // 声光提示完成
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_6);  // 蜂鸣器
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_7);  // LED
    delay_cycles(16000000);  // 1s
    DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_6);
    
    while(1);  // 停机
}
