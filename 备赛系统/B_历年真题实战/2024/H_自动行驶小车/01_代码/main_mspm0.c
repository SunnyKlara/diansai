/**
 * 2024H 自动行驶小车 - MSPM0G3507
 * 
 * 场地：220cm×120cm，两个半圆弧(R=40cm)
 * 特点：直线段无引导线！只有弧线段有黑线
 * 
 * 导航策略：
 *   弧线段 → 红外传感器循迹
 *   直线段 → 编码器+IMU航迹推算
 *   切换点 → 检测弧线结束/开始
 * 
 * 引脚：
 *   PA12~PA16  5路红外传感器
 *   PA0/PA1    左电机PWM
 *   PA2/PA3    右电机PWM
 *   PB0/PB1    左编码器
 *   PB2/PB3    右编码器
 *   PA6        蜂鸣器
 *   PA7        LED
 *   PB8        启动按键
 *   I2C        MPU6050 IMU
 */

#include "ti_msp_dl_config.h"
#include <math.h>

/* ========== 参数 ========== */
#define WHEEL_DIAMETER  65.0f    // 轮径 mm
#define WHEEL_BASE      130.0f   // 轮距 mm
#define ENCODER_PPR     330      // 编码器每转脉冲
#define PI              3.14159265f
#define MM_PER_PULSE    (PI * WHEEL_DIAMETER / ENCODER_PPR)  // 约0.619mm/脉冲

// 场地参数
#define STRAIGHT_LEN    1000.0f  // 直线段长度 mm (A→B或C→D)
#define ARC_RADIUS      400.0f   // 弧线半径 mm
#define ARC_LEN         (PI * ARC_RADIUS)  // 半圆弧长 ≈1257mm

// PID参数
#define KP_TRACK  25.0f
#define KD_TRACK  15.0f
#define KP_HEADING 3.0f  // 直线段航向保持

#define BASE_SPEED 55

/* ========== 状态机 ========== */
typedef enum {
    STATE_IDLE,
    STATE_STRAIGHT_AB,   // A→B直线
    STATE_ARC_BC,        // B→C弧线(右转)
    STATE_STRAIGHT_CD,   // C→D直线
    STATE_ARC_DA,        // D→A弧线(右转)
    STATE_DONE
} NavState_t;

static NavState_t nav_state = STATE_IDLE;
static int target_laps = 1;
static int current_lap = 0;

/* ========== 编码器 ========== */
static volatile int32_t enc_left = 0;
static volatile int32_t enc_right = 0;

float get_distance_mm(void) {
    float left_mm = enc_left * MM_PER_PULSE;
    float right_mm = enc_right * MM_PER_PULSE;
    return (left_mm + right_mm) / 2.0f;
}

void reset_distance(void) {
    enc_left = 0;
    enc_right = 0;
}

/* ========== IMU航向角 ========== */
static float heading_deg = 0;  // 当前航向角(度)，0=初始方向

// 简化版：通过MPU6050陀螺仪Z轴积分得到航向角
// 实际需要I2C读取MPU6050并积分
void update_heading(float gyro_z_dps, float dt) {
    heading_deg += gyro_z_dps * dt;
}

/* ========== 传感器 ========== */
typedef struct {
    uint8_t L2, L1, C, R1, R2;
    uint8_t any_black;  // 是否有任何传感器检测到黑线
} Sensors_t;

Sensors_t read_sensors(void) {
    Sensors_t s;
    s.L2 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_12) ? 0 : 1;
    s.L1 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_13) ? 0 : 1;
    s.C  = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_14) ? 0 : 1;
    s.R1 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_15) ? 0 : 1;
    s.R2 = DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_16) ? 0 : 1;
    s.any_black = s.L2 || s.L1 || s.C || s.R1 || s.R2;
    return s;
}

/* ========== 电机控制 ========== */
void motor_set(int left, int right) {
    // 限幅
    if (left > 100) left = 100;
    if (left < -100) left = -100;
    if (right > 100) right = 100;
    if (right < -100) right = -100;
    
    if (left >= 0) {
        DL_TimerA_setCaptureCompareValue(TIMA0, left * 10, DL_TIMER_CC_0_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_1_INDEX);
    } else {
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_0_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, (-left) * 10, DL_TIMER_CC_1_INDEX);
    }
    if (right >= 0) {
        DL_TimerA_setCaptureCompareValue(TIMA0, right * 10, DL_TIMER_CC_2_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_3_INDEX);
    } else {
        DL_TimerA_setCaptureCompareValue(TIMA0, 0, DL_TIMER_CC_2_INDEX);
        DL_TimerA_setCaptureCompareValue(TIMA0, (-right) * 10, DL_TIMER_CC_3_INDEX);
    }
}

/* ========== 声光提示 ========== */
void beep_and_flash(void) {
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_6);   // 蜂鸣器
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_7);   // LED
    delay_cycles(4000000);  // 250ms
    DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_6);
    DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_7);
}

/* ========== 导航控制 ========== */

// 直线段：编码器计距 + IMU保持航向
void navigate_straight(float target_dist_mm, float target_heading) {
    reset_distance();
    
    while (get_distance_mm() < target_dist_mm) {
        // 航向偏差
        float heading_error = target_heading - heading_deg;
        float correction = KP_HEADING * heading_error;
        
        int left  = BASE_SPEED - (int)correction;
        int right = BASE_SPEED + (int)correction;
        motor_set(left, right);
        
        delay_cycles(1600000);  // 10ms
        // 这里应该调用update_heading()更新航向
    }
}

// 弧线段：红外循迹
void navigate_arc(void) {
    static float prev_error = 0;
    
    // 循迹直到检测不到黑线（弧线结束）
    uint16_t no_line_count = 0;
    
    while (1) {
        Sensors_t s = read_sensors();
        
        if (!s.any_black) {
            no_line_count++;
            if (no_line_count > 20) break;  // 连续200ms没有黑线=弧线结束
        } else {
            no_line_count = 0;
        }
        
        // PID循迹
        float weighted = -2.0f*s.L2 - 1.0f*s.L1 + 0.0f*s.C + 1.0f*s.R1 + 2.0f*s.R2;
        float sum = s.L2 + s.L1 + s.C + s.R1 + s.R2;
        float error = (sum > 0) ? weighted / sum : prev_error;
        
        float derivative = error - prev_error;
        prev_error = error;
        
        float correction = KP_TRACK * error + KD_TRACK * derivative;
        
        int left  = BASE_SPEED - (int)correction;
        int right = BASE_SPEED + (int)correction;
        motor_set(left, right);
        
        delay_cycles(1600000);  // 10ms
    }
}

/* ========== 主程序 ========== */
int main(void) {
    SYSCFG_DL_init();
    
    // 等待启动按键
    while (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_8)) {
        delay_cycles(160000);
    }
    delay_cycles(16000000);  // 1s消抖
    
    beep_and_flash();  // 启动提示
    
    // 要求(2)路径: A→B→弧→C→D→弧→A
    for (current_lap = 0; current_lap < target_laps; current_lap++) {
        
        // A→B 直线 (航向0°)
        navigate_straight(STRAIGHT_LEN, 0);
        beep_and_flash();  // 到达B点
        
        // B→C 弧线 (右转半圆)
        navigate_arc();
        beep_and_flash();  // 到达C点
        
        // C→D 直线 (航向180°，反方向)
        navigate_straight(STRAIGHT_LEN, 180.0f);
        beep_and_flash();  // 到达D点
        
        // D→A 弧线 (右转半圆)
        navigate_arc();
        beep_and_flash();  // 到达A点
    }
    
    // 停车
    motor_set(0, 0);
    
    // 持续声光提示
    while (1) {
        DL_GPIO_togglePins(GPIOA, DL_GPIO_PIN_7);
        DL_GPIO_togglePins(GPIOA, DL_GPIO_PIN_6);
        delay_cycles(8000000);  // 0.5s
    }
}
