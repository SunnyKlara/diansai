/**
 * 2023E 绿色光斑自动追踪控制器
 * 
 * 【这道题最核心的工程挑战】
 * 
 * 1. 两套系统完全独立，不能通信。
 *    绿色系统只能通过摄像头"看"红色光斑来追踪。
 *    这意味着：
 *    - 摄像头必须能同时看到红色和绿色光斑
 *    - 或者摄像头只看红色，绿色靠开环预测
 *    
 *    【我的选择】摄像头固定在绿色云台旁边（不随云台转），
 *    看整个屏幕，检测红色光斑位置，然后计算绿色云台应该指向哪里。
 *    
 * 2. 追踪延迟是致命的。
 *    OpenMV帧率约30fps → 33ms延迟
 *    串口传输 → 5ms延迟
 *    PID计算 → 1ms
 *    舵机响应 → 50~100ms
 *    总延迟约100ms
 *    
 *    红色光斑速度：2000mm/30s ≈ 67mm/s
 *    100ms延迟 → 6.7mm滞后
 *    要求3cm以内 → 勉强够，但没有余量
 *    
 *    【优化策略】加入速度前馈：
 *    预测红色光斑下一帧的位置 = 当前位置 + 速度 × 延迟时间
 *    
 * 3. 摄像头安装位置决定了坐标变换的复杂度。
 *    如果摄像头在云台上（随云台转）→ 红色光斑始终在图像中心附近 → 简单
 *    如果摄像头固定（不随云台转）→ 需要全局坐标变换 → 复杂但更稳定
 *    
 *    【我的选择】摄像头固定安装，不随云台转。
 *    理由：随云台转的话，云台运动会导致图像模糊，影响检测精度。
 */

#include "stm32f1xx_hal.h"
#include <math.h>

#define PI 3.14159265f
#define SCREEN_DIST 1000.0f

/* ========== 从OpenMV接收红色光斑位置 ========== */

// OpenMV发送格式: "$cx,cy,vx,vy\n"
// cx,cy: 红色光斑在图像中的像素坐标（相对图像中心）
// vx,vy: 红色光斑的速度（像素/帧）—— 用于前馈预测

typedef struct {
    float cx, cy;      // 位置（像素）
    float vx, vy;      // 速度（像素/帧）
    uint8_t valid;      // 是否检测到
    uint32_t timestamp; // 时间戳
} TargetInfo_t;

static volatile TargetInfo_t target = {0};

// 串口接收解析（中断中调用）
void parse_target_data(char* buf)
{
    float cx, cy, vx, vy;
    if (sscanf(buf, "$%f,%f,%f,%f", &cx, &cy, &vx, &vy) == 4) {
        target.cx = cx;
        target.cy = cy;
        target.vx = vx;
        target.vy = vy;
        target.valid = 1;
        target.timestamp = HAL_GetTick();
    }
}

/* ========== 像素坐标→屏幕物理坐标 ========== */

/**
 * 【标定参数】
 * 摄像头固定安装，拍摄整个屏幕。
 * 需要标定：图像像素坐标 → 屏幕物理坐标(mm)的映射。
 * 
 * 方法：在屏幕四角放标记点，记录像素坐标，计算单应性矩阵。
 * 简化版：假设摄像头正对屏幕中心，用线性映射。
 * 
 * 标定时测量：
 *   屏幕左上角在图像中的像素坐标 → (px_left, py_top)
 *   屏幕右下角在图像中的像素坐标 → (px_right, py_bottom)
 *   屏幕物理尺寸：500mm × 500mm
 */
static float px_to_mm_x = 1.0f;  // mm/像素，需要标定
static float px_to_mm_y = 1.0f;
static float px_offset_x = 0;    // 图像中心对应的屏幕坐标偏移
static float px_offset_y = 0;

void calibrate_camera(float screen_width_px, float screen_height_px)
{
    px_to_mm_x = 500.0f / screen_width_px;   // 500mm屏幕 / 像素宽度
    px_to_mm_y = 500.0f / screen_height_px;
}

void pixel_to_screen_mm(float px, float py, float* sx, float* sy)
{
    *sx = (px - px_offset_x) * px_to_mm_x;
    *sy = (py - px_offset_y) * px_to_mm_y;
}

/* ========== 屏幕坐标→云台角度 ========== */

/**
 * 【关键】绿色激光笔不在屏幕正前方！
 * 绿色激光笔在红色激光笔侧面0.4~1.0m处。
 * 所以同一个屏幕坐标，红色和绿色需要不同的云台角度。
 * 
 * 设绿色激光笔位置为(Gx, 0)（在红色激光笔右侧Gx处）
 * 屏幕上目标点为(Sx, Sy)
 * 绿色云台偏航角 = atan((Sx - Gx) / D)
 * 绿色云台俯仰角 = atan(Sy / sqrt((Sx-Gx)² + D²))
 * 
 * 这个几何关系很多人会忽略，直接用红色的角度公式，导致偏差！
 */
static float green_offset_x = 500.0f;  // 绿色激光笔相对红色的X偏移(mm)

typedef struct {
    float yaw_deg;
    float pitch_deg;
} Angle_t;

Angle_t screen_to_green_angle(float sx_mm, float sy_mm)
{
    Angle_t a;
    float dx = sx_mm - green_offset_x;  // 目标相对绿色激光笔的X距离
    float dist_h = sqrtf(dx * dx + SCREEN_DIST * SCREEN_DIST);
    
    a.yaw_deg = atanf(dx / SCREEN_DIST) * 180.0f / PI;
    a.pitch_deg = atanf(sy_mm / dist_h) * 180.0f / PI;
    
    return a;
}

/* ========== PID追踪控制器 ========== */

typedef struct {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float output;
} PIDAxis_t;

static PIDAxis_t pid_yaw  = {0.08f, 0.002f, 0.04f, 0, 0, 0};
static PIDAxis_t pid_pitch = {0.08f, 0.002f, 0.04f, 0, 0, 0};

// 当前舵机角度
static float cur_yaw_deg = 0;
static float cur_pitch_deg = 0;

float pid_update(PIDAxis_t* pid, float error)
{
    pid->integral += error;
    // 积分限幅（防饱和）
    if (pid->integral > 200) pid->integral = 200;
    if (pid->integral < -200) pid->integral = -200;
    
    float deriv = error - pid->prev_error;
    pid->prev_error = error;
    
    pid->output = pid->kp * error + pid->ki * pid->integral + pid->kd * deriv;
    return pid->output;
}

/* ========== 主追踪函数（20ms周期调用） ========== */

extern TIM_HandleTypeDef htim2;

void tracker_update(void)
{
    if (!target.valid) return;
    
    // 检查数据新鲜度（超过200ms认为丢失）
    if (HAL_GetTick() - target.timestamp > 200) {
        target.valid = 0;
        return;
    }
    
    // 1. 像素→屏幕物理坐标
    float sx, sy;
    pixel_to_screen_mm(target.cx, target.cy, &sx, &sy);
    
    // 2. 【速度前馈预测】补偿100ms延迟
    float predict_ms = 100.0f;  // 预测提前量
    float vx_mm = target.vx * px_to_mm_x;  // 像素速度→mm速度
    float vy_mm = target.vy * px_to_mm_y;
    sx += vx_mm * predict_ms / 33.0f;  // 33ms/帧
    sy += vy_mm * predict_ms / 33.0f;
    
    // 3. 屏幕坐标→目标云台角度
    Angle_t target_angle = screen_to_green_angle(sx, sy);
    
    // 4. PID控制
    float yaw_error = target_angle.yaw_deg - cur_yaw_deg;
    float pitch_error = target_angle.pitch_deg - cur_pitch_deg;
    
    float yaw_adj = pid_update(&pid_yaw, yaw_error);
    float pitch_adj = pid_update(&pid_pitch, pitch_error);
    
    cur_yaw_deg += yaw_adj;
    cur_pitch_deg += pitch_adj;
    
    // 限幅
    if (cur_yaw_deg > 30) cur_yaw_deg = 30;
    if (cur_yaw_deg < -30) cur_yaw_deg = -30;
    if (cur_pitch_deg > 20) cur_pitch_deg = 20;
    if (cur_pitch_deg < -20) cur_pitch_deg = -20;
    
    // 5. 输出到舵机
    // 舵机中位=90°=1500μs，偏航范围约±30°
    uint16_t yaw_us = 1500 + (int16_t)(cur_yaw_deg * 11.1f);
    uint16_t pitch_us = 1500 + (int16_t)(cur_pitch_deg * 11.1f);
    
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, yaw_us);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pitch_us);
}
