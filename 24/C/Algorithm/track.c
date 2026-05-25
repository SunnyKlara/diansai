#include "track.h"
#include "motor.h"

/* ============ PID控制器实例 ============ */
static PID_Controller pid_direction;   // 方向PID (转向环)
static PID_Controller pid_speed_left;  // 左轮速度PID
static PID_Controller pid_speed_right; // 右轮速度PID

/* ============ 状态变量 ============ */
static CarState g_car_state = CAR_IDLE;
static SpeedProfile g_speed = {
    .straight_speed = 300,   // 直道基础速度
    .turn_speed     = 180,   // 弯道速度
    .cross_speed    = 200    // 路口速度
};

/* ============ 丢线计数器 ============ */
static uint16_t lost_counter = 0;
#define LOST_TIMEOUT  50   // 丢线超时 (50*20ms = 1s)

/**
 * @brief 初始化循迹控制系统
 */
void Track_Init(void)
{
    /* 方向PID: 位置式
     * 输入: 传感器偏差 (-30 ~ +30)
     * 输出: 左右轮速度差 (-500 ~ +500)
     * 
     * 调参建议:
     * 1. 先只用P, 从小到大调, 直到小车能基本循迹
     * 2. 加D, 减小振荡
     * 3. 一般不需要I (循迹不需要消除稳态误差)
     */
    PID_Init(&pid_direction, 8.0f, 0.0f, 3.0f, -500.0f, 500.0f);
    PID_SetTarget(&pid_direction, 0.0f);  // 目标: 偏差为0 (在线上)
    
    /* 速度PID: 增量式
     * 输入: 编码器速度 (脉冲/周期)
     * 输出: PWM值 (0 ~ 999)
     */
    PID_Init(&pid_speed_left,  10.0f, 1.0f, 0.0f, -999.0f, 999.0f);
    PID_Init(&pid_speed_right, 10.0f, 1.0f, 0.0f, -999.0f, 999.0f);
    
    g_car_state = CAR_IDLE;
}

/**
 * @brief 循迹主控制循环 (核心!)
 * @note  调用频率: 50Hz (20ms)
 * 
 * 控制流程:
 * 1. 读取灰度传感器
 * 2. 计算偏差
 * 3. 检测路况 (弯道/路口)
 * 4. 方向PID计算转向量
 * 5. 根据路况调整基础速度
 * 6. 差速输出到电机
 */
void Track_Control(void)
{
    if (g_car_state == CAR_IDLE || g_car_state == CAR_STOPPED)
        return;
    
    /* ---- Step 1: 读取传感器 ---- */
    SensorData *sensor = Sensor_GetData();
    Sensor_ReadAll(sensor);
    
    /* ---- Step 2: 计算偏差 ---- */
    float position = Sensor_CalcPosition(sensor);
    
    /* ---- Step 3: 检测路况 ---- */
    Sensor_DetectPattern(sensor);
    
    /* ---- Step 4: 丢线处理 ---- */
    if (sensor->line_lost) {
        lost_counter++;
        if (lost_counter > LOST_TIMEOUT) {
            /* 丢线超时，停车 */
            Motor_Stop();
            g_car_state = CAR_LOST;
            return;
        }
        /* 丢线未超时，继续用上次偏差方向大幅修正 */
    } else {
        lost_counter = 0;
    }
    
    /* ---- Step 5: 方向PID ---- */
    float turn_output = PID_Compute(&pid_direction, position);
    
    /* ---- Step 6: 根据路况选择基础速度 ---- */
    int16_t base_speed;
    
    if (sensor->cross_detected) {
        g_car_state = CAR_CROSS;
        base_speed = g_speed.cross_speed;
    } else if (sensor->turn_detected) {
        g_car_state = CAR_TURNING;
        base_speed = g_speed.turn_speed;
    } else {
        g_car_state = CAR_RUNNING;
        base_speed = g_speed.straight_speed;
    }
    
    /* ---- Step 7: 差速输出 ---- */
    /* 偏差为负(偏左) -> turn_output为正 -> 左轮加速右轮减速 -> 右转回线 */
    int16_t left_pwm  = base_speed + (int16_t)turn_output;
    int16_t right_pwm = base_speed - (int16_t)turn_output;
    
    /* 限幅 */
    if (left_pwm > MOTOR_PWM_MAX) left_pwm = MOTOR_PWM_MAX;
    if (left_pwm < -MOTOR_PWM_MAX) left_pwm = -MOTOR_PWM_MAX;
    if (right_pwm > MOTOR_PWM_MAX) right_pwm = MOTOR_PWM_MAX;
    if (right_pwm < -MOTOR_PWM_MAX) right_pwm = -MOTOR_PWM_MAX;
    
    Motor_SetBoth(left_pwm, right_pwm);
}

void Track_Start(void)
{
    PID_Reset(&pid_direction);
    PID_Reset(&pid_speed_left);
    PID_Reset(&pid_speed_right);
    lost_counter = 0;
    g_car_state = CAR_RUNNING;
}

void Track_Stop(void)
{
    Motor_Stop();
    g_car_state = CAR_STOPPED;
}

CarState Track_GetState(void)
{
    return g_car_state;
}

void Track_SetSpeed(int16_t straight, int16_t turn)
{
    g_speed.straight_speed = straight;
    g_speed.turn_speed = turn;
}
