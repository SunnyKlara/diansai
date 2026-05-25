/**
 * 2022A 功率因数可调控制器
 * 
 * 【这道题的精髓】
 * 
 * 表面上看是"模拟不同类型负载"，本质上是：
 * 控制输入电流的幅度和相位，使其与电压形成指定的功率因数关系。
 * 
 * 【为什么dq变换是最优方案】
 * 
 * 方案1：直接控制电流相位
 *   生成 i_ref = I × sin(ωt + φ)，φ由cosφ设定值决定
 *   问题：需要精确的电压相位检测（PLL），φ的精度直接取决于PLL精度
 *   
 * 方案2：dq坐标变换（推荐）
 *   将交流量变换到旋转坐标系，变成直流量
 *   id = I × cosφ（有功分量）
 *   iq = I × sinφ（无功分量）
 *   分别用PI控制器控制id和iq → 自然实现功率因数可调
 *   优势：PI控制直流量比控制交流量容易得多，精度更高
 * 
 * 【容性vs感性的区别】
 *   感性：电流滞后电压 → iq < 0
 *   容性：电流超前电压 → iq > 0
 *   电阻性：iq = 0
 * 
 * 【一个容易忽略的问题】
 * 单相系统不能直接做dq变换（dq变换需要两相正交信号）！
 * 解决方法：用全通滤波器或延时1/4周期生成虚拟的β分量
 *   α = 实际信号
 *   β = α延迟90°
 * 然后对(α, β)做Park变换得到(d, q)
 */

#include "stm32g4xx_hal.h"
#include "arm_math.h"

/* ========== PLL锁相环：检测电压相位 ========== */

/**
 * 单相PLL（基于SOGI：二阶广义积分器）
 * 
 * 为什么不用简单的过零检测？
 * 过零检测只能在过零点更新相位，两个过零点之间是开环的。
 * SOGI-PLL是连续闭环的，相位精度高得多。
 */
typedef struct {
    float theta;      // 电压相位角 (rad)
    float omega;      // 角频率 (rad/s)
    float alpha;      // α分量（与电压同相）
    float beta;       // β分量（滞后90°）
    
    // SOGI内部状态
    float v1, v2;
    
    // PLL PI参数
    float kp_pll, ki_pll;
    float integral_pll;
} PLL_t;

static PLL_t pll = {
    .theta = 0, .omega = 314.159f,  // 初始50Hz
    .v1 = 0, .v2 = 0,
    .kp_pll = 100.0f, .ki_pll = 5000.0f,
    .integral_pll = 314.159f
};

/**
 * PLL更新（在ADC中断中调用，20kHz）
 * @param v_grid 电网电压采样值（归一化到±1）
 */
void pll_update(float v_grid, float dt)
{
    // SOGI生成α和β
    float k = 1.414f;  // SOGI增益
    float err = v_grid - pll.alpha;
    
    pll.v1 += (k * err - pll.v2) * pll.omega * dt;
    pll.v2 += pll.v1 * pll.omega * dt;
    
    pll.alpha = pll.v1;
    pll.beta = pll.v2;
    
    // Park变换提取d分量（应该为0，如果锁定的话）
    float cos_t = arm_cos_f32(pll.theta);
    float sin_t = arm_sin_f32(pll.theta);
    
    float vd = pll.alpha * cos_t + pll.beta * sin_t;
    float vq = -pll.alpha * sin_t + pll.beta * cos_t;
    
    // PI控制器锁定vq=0
    float freq_err = -vq;  // vq=0时锁定
    pll.integral_pll += pll.ki_pll * freq_err * dt;
    pll.omega = pll.kp_pll * freq_err + pll.integral_pll;
    
    // 限幅（防止频率跑飞）
    if (pll.omega > 340) pll.omega = 340;  // 54Hz
    if (pll.omega < 290) pll.omega = 290;  // 46Hz
    
    // 更新相位
    pll.theta += pll.omega * dt;
    if (pll.theta > 2 * 3.14159f) pll.theta -= 2 * 3.14159f;
    if (pll.theta < 0) pll.theta += 2 * 3.14159f;
}

/* ========== 功率因数控制 ========== */

typedef struct {
    float cos_phi_set;   // 设定的功率因数 (0.5~1.0)
    int8_t load_type;    // 1=电阻, 2=电感, 3=电容
    float i_amplitude;   // 电流幅值设定 (A)
    
    // dq电流参考
    float id_ref;        // 有功电流参考
    float iq_ref;        // 无功电流参考
    
    // dq电流PI控制器
    float kp_id, ki_id;
    float kp_iq, ki_iq;
    float id_integral, iq_integral;
} PFControl_t;

static PFControl_t pfc = {
    .cos_phi_set = 1.0f,
    .load_type = 1,
    .i_amplitude = 2.0f,
    .kp_id = 5.0f, .ki_id = 500.0f,
    .kp_iq = 5.0f, .ki_iq = 500.0f,
    .id_integral = 0, .iq_integral = 0
};

/**
 * 设置功率因数
 * @param cos_phi  功率因数 (0.5~1.0)
 * @param type     负载类型 (1=R, 2=L, 3=C)
 * @param i_amp    电流幅值 (A)
 */
void set_power_factor(float cos_phi, int8_t type, float i_amp)
{
    pfc.cos_phi_set = cos_phi;
    pfc.load_type = type;
    pfc.i_amplitude = i_amp;
    
    float sin_phi = sqrtf(1.0f - cos_phi * cos_phi);
    
    // 计算dq电流参考
    pfc.id_ref = i_amp * cos_phi;  // 有功分量（始终为正）
    
    switch (type) {
        case 1:  // 电阻性
            pfc.iq_ref = 0;
            break;
        case 2:  // 电感性（电流滞后→iq为负）
            pfc.iq_ref = -i_amp * sin_phi;
            break;
        case 3:  // 电容性（电流超前→iq为正）
            pfc.iq_ref = i_amp * sin_phi;
            break;
    }
}

/**
 * 电流控制主函数（20kHz调用）
 * @param i_actual  实际输入电流采样值 (A)
 * @return PWM占空比 (0~1)
 */
float current_control(float i_actual, float dt)
{
    // 1. 将实际电流变换到dq坐标
    float cos_t = arm_cos_f32(pll.theta);
    float sin_t = arm_sin_f32(pll.theta);
    
    // 单相→虚拟两相（用延迟90°的方法）
    // 简化：直接用PLL的alpha/beta作为参考
    float i_alpha = i_actual;
    // i_beta需要延迟90°，用全通滤波器或缓冲区
    static float i_buf[100];  // 100点缓冲 = 1/4周期@20kHz/50Hz
    static uint8_t buf_idx = 0;
    float i_beta = i_buf[buf_idx];  // 延迟100点 = 5ms = 90°@50Hz
    i_buf[buf_idx] = i_actual;
    buf_idx = (buf_idx + 1) % 100;
    
    // Park变换
    float id = i_alpha * cos_t + i_beta * sin_t;
    float iq = -i_alpha * sin_t + i_beta * cos_t;
    
    // 2. dq轴PI控制
    float id_err = pfc.id_ref - id;
    float iq_err = pfc.iq_ref - iq;
    
    pfc.id_integral += pfc.ki_id * id_err * dt;
    pfc.iq_integral += pfc.ki_iq * iq_err * dt;
    
    // 积分限幅
    if (pfc.id_integral > 0.8f) pfc.id_integral = 0.8f;
    if (pfc.id_integral < -0.8f) pfc.id_integral = -0.8f;
    if (pfc.iq_integral > 0.8f) pfc.iq_integral = 0.8f;
    if (pfc.iq_integral < -0.8f) pfc.iq_integral = -0.8f;
    
    float vd_out = pfc.kp_id * id_err + pfc.id_integral;
    float vq_out = pfc.kp_iq * iq_err + pfc.iq_integral;
    
    // 3. 逆Park变换→得到PWM参考
    float v_alpha = vd_out * cos_t - vq_out * sin_t;
    // float v_beta = vd_out * sin_t + vq_out * cos_t;  // 单相不需要
    
    // 4. 转换为占空比
    float duty = 0.5f + v_alpha * 0.5f;
    if (duty > 0.95f) duty = 0.95f;
    if (duty < 0.05f) duty = 0.05f;
    
    return duty;
}
