/**
 * 2024F 磁悬浮控制核心算法
 * 
 * ============================================================
 * 【磁悬浮PID调参——为什么比其他系统难100倍？】
 * ============================================================
 * 
 * 普通系统（如电压稳压、电机调速）是**开环稳定**的：
 *   即使没有控制器，系统也不会发散。
 *   PID只是让它更快更准。
 *   调参时如果Kp太大，最多振荡，不会炸。
 * 
 * 磁悬浮是**开环不稳定**的：
 *   没有控制器，悬浮盘立刻掉下来（或吸上去）。
 *   PID必须足够快、足够准才能维持悬浮。
 *   调参时如果Kp太小，悬浮盘直接掉；Kp太大，剧烈振荡后掉。
 *   **可调范围非常窄！**
 * 
 * 这就是为什么磁悬浮是PID调参的终极考验。
 * 
 * ============================================================
 * 【PD控制 vs PID控制】
 * ============================================================
 * 
 * 对于磁悬浮，**PD控制比PID更合适**！
 * 
 * 原因：
 * - P项提供恢复力（偏离设定值→产生纠正力）
 * - D项提供阻尼（运动速度→产生制动力）
 * - I项在不稳定系统中是危险的：
 *   积分累积会导致控制量缓慢漂移，
 *   在某个瞬间突然超过临界值，悬浮盘弹飞。
 * 
 * 所以：先用PD控制实现稳定悬浮，
 *       如果有稳态误差再小心地加一点I。
 * 
 * ============================================================
 */

#include "stm32f4xx_hal.h"
#include <math.h>

/* ========== 系统参数 ========== */
#define CONTROL_FREQ    2000    // 控制频率2kHz（必须足够快！）
#define NUM_COILS       5       // 5个电磁铁（1中心+4角）
#define HALL_CENTER     0       // 中心霍尔传感器索引
#define HALL_FRONT      1       // 前方
#define HALL_BACK       2       // 后方
#define HALL_LEFT       3       // 左方
#define HALL_RIGHT      4       // 右方

/* ========== 霍尔传感器 ========== */

/**
 * 【霍尔传感器的选型和安装】
 * 
 * SS49E：线性霍尔传感器
 *   灵敏度：1.4mV/Gauss
 *   输出：2.5V ± 1.4mV/G × B
 *   测量范围：约±700G
 * 
 * 安装位置：在电磁铁的中心孔中
 *   电磁铁通电时，霍尔传感器测量的是
 *   电磁铁磁场 + 悬浮盘反馈磁场的叠加。
 *   悬浮盘越近 → 磁路磁阻越小 → 磁场越强 → 霍尔输出越大
 *   
 * 【关键】霍尔传感器测量的不是距离，而是磁场强度。
 * 需要标定：霍尔输出电压 → 悬浮盘距离 的映射关系。
 * 这个映射是非线性的（近似1/d²关系）。
 */

// ADC通道分配
static const uint32_t hall_adc_ch[NUM_COILS] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, 
    ADC_CHANNEL_3, ADC_CHANNEL_4
};

// 霍尔传感器标定参数（需要实测！）
// hall_voltage → distance_mm 的查找表
#define CAL_POINTS 10
static float cal_voltage[CAL_POINTS] = {1.5, 1.7, 1.9, 2.1, 2.3, 2.5, 2.7, 2.9, 3.1, 3.3};
static float cal_distance[CAL_POINTS] = {50, 40, 32, 26, 21, 17, 14, 11, 9, 7}; // mm

float hall_to_distance(float voltage)
{
    // 线性插值
    if (voltage <= cal_voltage[0]) return cal_distance[0];
    if (voltage >= cal_voltage[CAL_POINTS-1]) return cal_distance[CAL_POINTS-1];
    
    for (int i = 0; i < CAL_POINTS-1; i++) {
        if (voltage >= cal_voltage[i] && voltage < cal_voltage[i+1]) {
            float t = (voltage - cal_voltage[i]) / (cal_voltage[i+1] - cal_voltage[i]);
            return cal_distance[i] * (1-t) + cal_distance[i+1] * t;
        }
    }
    return 20.0f;  // 默认
}

/* ========== PD控制器 ========== */

/**
 * 【为什么用PD而不是PID？】
 * 
 * 见文件开头的分析。
 * 
 * 【D项的实现细节】
 * 
 * 直接对误差求导会放大噪声。
 * 更好的方法：对测量值求导（而不是误差）。
 * 
 * 原因：设定值通常是常数，d(error)/dt = d(setpoint)/dt - d(measurement)/dt
 *       = 0 - d(measurement)/dt = -d(measurement)/dt
 * 所以对测量值求导和对误差求导结果相同（差个负号），
 * 但当设定值突变时（如改变悬浮高度），
 * 对误差求导会产生巨大的脉冲（微分冲击），
 * 对测量值求导则不会。
 * 
 * 这叫"微分先行"或"Derivative on Measurement"。
 */

typedef struct {
    float kp;
    float kd;
    float ki;           // 很小或为0
    float integral;
    float prev_measurement;  // 上一次测量值（用于微分）
    float output;
    float output_min, output_max;
} PDController_t;

// 中心电磁铁控制器（主悬浮力）
static PDController_t ctrl_center = {
    .kp = 15.0f,       // 需要实际调参！
    .kd = 0.8f,        // D项非常关键
    .ki = 0.1f,        // 很小的I项
    .integral = 0,
    .prev_measurement = 20.0f,
    .output_min = 0,
    .output_max = 0.95f
};

float pd_compute(PDController_t* c, float setpoint, float measurement, float dt)
{
    float error = setpoint - measurement;
    
    // P项
    float p_term = c->kp * error;
    
    // D项（微分先行：对测量值求导）
    float d_measurement = (measurement - c->prev_measurement) / dt;
    float d_term = -c->kd * d_measurement;  // 注意负号！
    c->prev_measurement = measurement;
    
    // I项（很小，防止稳态误差）
    c->integral += c->ki * error * dt;
    if (c->integral > c->output_max * 0.3f) c->integral = c->output_max * 0.3f;
    if (c->integral < 0) c->integral = 0;  // 电磁铁只能吸不能推
    
    // 输出
    c->output = p_term + d_term + c->integral;
    
    // 限幅
    if (c->output > c->output_max) c->output = c->output_max;
    if (c->output < c->output_min) c->output = c->output_min;
    
    return c->output;
}

/* ========== 电磁铁驱动 ========== */

/**
 * 【PWM驱动电磁铁】
 * 
 * 电磁铁是电感性负载，不能直接用电压驱动。
 * 用PWM+续流二极管驱动：
 *   PWM高→电流增加→磁力增加
 *   PWM低→电流通过续流二极管衰减
 *   
 * PWM频率选择：
 *   太低(<1kHz)：电流纹波大，悬浮盘会振动
 *   太高(>50kHz)：开关损耗大
 *   推荐：10~20kHz
 * 
 * 【电流模式 vs 电压模式】
 * 
 * 电压模式：PWM占空比控制电压 → 电流由电感决定 → 响应慢
 * 电流模式：加电流采样+内环PI → 直接控制电流 → 响应快
 * 
 * 磁悬浮需要快速响应 → 推荐电流模式。
 * 但电流模式增加了复杂度，如果时间不够可以先用电压模式。
 */

extern TIM_HandleTypeDef htim1;  // 5通道PWM

void set_coil_duty(uint8_t coil_idx, float duty)
{
    if (duty < 0) duty = 0;
    if (duty > 0.95f) duty = 0.95f;
    
    uint16_t ccr = (uint16_t)(duty * htim1.Instance->ARR);
    
    switch (coil_idx) {
        case 0: htim1.Instance->CCR1 = ccr; break;
        case 1: htim1.Instance->CCR2 = ccr; break;
        case 2: htim1.Instance->CCR3 = ccr; break;
        case 3: htim1.Instance->CCR4 = ccr; break;
        // 第5个通道用TIM8或其他定时器
    }
}

/* ========== 主控制循环（2kHz定时器中断） ========== */

static float height_setpoint = 20.0f;  // 设定悬浮高度(mm)

void maglev_control_tick(void)
{
    float dt = 1.0f / CONTROL_FREQ;
    
    // 1. 读取霍尔传感器
    float hall_v = read_adc_voltage(hall_adc_ch[HALL_CENTER]);
    float height = hall_to_distance(hall_v);
    
    // 2. PD控制
    float duty = pd_compute(&ctrl_center, height_setpoint, height, dt);
    
    // 3. 输出到电磁铁
    set_coil_duty(0, duty);
    
    // 4. 水平稳定（如果有多个电磁铁）
    // 读取四角霍尔传感器，用PD控制保持水平
    // ... 类似的PD控制，但Kp和Kd更小
}

/* ========== 高度可调（要求4） ========== */

/**
 * 【高度调节的陷阱】
 * 
 * 不能直接跳变设定值！
 * 如果从20mm突然改到40mm，控制器会猛减电流，
 * 悬浮盘可能直接掉下来。
 * 
 * 正确做法：用斜坡或S曲线缓慢改变设定值。
 * 每次改变0.2mm，间隔10ms → 2cm需要1秒。
 */
void set_height_smooth(float target_mm)
{
    float step = 0.2f;  // 每步0.2mm
    float current = height_setpoint;
    
    while (fabsf(current - target_mm) > step) {
        if (target_mm > current) {
            current += step;
        } else {
            current -= step;
        }
        height_setpoint = current;
        HAL_Delay(10);  // 10ms/步
    }
    height_setpoint = target_mm;
}
