/**
 * 2025B 有源电力滤波器(APF)控制模块
 * 功能：产生补偿电流iF，抵消负载电流iL中的谐波分量
 * 
 * 原理：
 *   iS = iL - iF
 *   如果 iF = iL的谐波分量，则 iS = iL的基波分量（纯正弦）
 * 
 * 实现方法：
 *   1. 实时采样iL
 *   2. 提取iL的基波分量（用数字带通滤波器或dq变换）
 *   3. iF_ref = iL - iL_fundamental（谐波分量）
 *   4. 电流跟踪控制：让APF实际输出的iF跟踪iF_ref
 */

#include "stm32f4xx_hal.h"
#include "arm_math.h"

/* ========== 参数 ========== */
#define APF_SAMPLE_RATE  20000   // APF控制频率 20kHz
#define VDC_APF          48.0f  // APF直流母线电压
#define L_APF            2.0e-3f // APF输出电感 2mH

// 电流跟踪PI参数
#define KP_IF   5.0f
#define KI_IF   500.0f

/* ========== 基波提取：滑动平均法 ========== */
// 对iL做一个周期(20ms=400个采样点@20kHz)的滑动平均
// 滑动平均的结果就是基波+直流分量
// 但更好的方法是用dq变换或自适应陷波器

#define CYCLE_SAMPLES  (APF_SAMPLE_RATE / 50)  // 400点/周期

static float il_buffer[CYCLE_SAMPLES];  // 一个周期的iL缓冲
static uint16_t buf_idx = 0;
static float il_sum = 0;

/**
 * 简易基波提取（滑动窗口DFT法）
 * 提取50Hz基波分量的瞬时值
 * 
 * 原理：对最近一个周期的数据做50Hz的DFT
 *       得到基波的幅值和相位
 *       然后重建基波瞬时值
 */

static float sin_table[CYCLE_SAMPLES];
static float cos_table[CYCLE_SAMPLES];
static uint8_t tables_inited = 0;

void APF_Init(void)
{
    // 预计算sin/cos表
    for (int i = 0; i < CYCLE_SAMPLES; i++) {
        float angle = 2.0f * 3.14159265f * i / CYCLE_SAMPLES;
        sin_table[i] = arm_sin_f32(angle);
        cos_table[i] = arm_cos_f32(angle);
    }
    tables_inited = 1;
    
    // 清零缓冲区
    for (int i = 0; i < CYCLE_SAMPLES; i++) {
        il_buffer[i] = 0;
    }
    buf_idx = 0;
    il_sum = 0;
}

/**
 * 提取基波并计算补偿电流参考值
 * @param il_now 当前采样的负载电流值 (A)
 * @return 补偿电流参考值 iF_ref (A)
 * 
 * 在20kHz定时器中断中调用
 */
float APF_CalcReference(float il_now)
{
    // 更新环形缓冲区
    il_buffer[buf_idx] = il_now;
    
    // 用DFT提取基波 (只算50Hz那一个频率点)
    float sum_sin = 0, sum_cos = 0;
    for (int i = 0; i < CYCLE_SAMPLES; i++) {
        int idx = (buf_idx - i + CYCLE_SAMPLES) % CYCLE_SAMPLES;
        sum_sin += il_buffer[idx] * sin_table[i];
        sum_cos += il_buffer[idx] * cos_table[i];
    }
    
    // 基波幅值和相位
    float a1 = 2.0f * sum_sin / CYCLE_SAMPLES;
    float b1 = 2.0f * sum_cos / CYCLE_SAMPLES;
    
    // 重建当前时刻的基波瞬时值
    float il_fundamental = a1 * sin_table[buf_idx] + b1 * cos_table[buf_idx];
    
    // 补偿电流 = 总电流 - 基波 = 谐波分量
    float if_ref = il_now - il_fundamental;
    
    buf_idx = (buf_idx + 1) % CYCLE_SAMPLES;
    
    return if_ref;
}

/**
 * APF电流跟踪控制
 * 让APF实际输出电流跟踪参考值
 * @param if_ref  参考电流 (A)
 * @param if_act  实际电流 (A, 从电流传感器读取)
 * @return PWM占空比 (0~1)
 */
static float if_integral = 0;

float APF_CurrentLoop(float if_ref, float if_act)
{
    float error = if_ref - if_act;
    
    // PI控制器
    if_integral += KI_IF * error / APF_SAMPLE_RATE;
    if (if_integral > 0.9f) if_integral = 0.9f;
    if (if_integral < -0.9f) if_integral = -0.9f;
    
    float duty = 0.5f + KP_IF * error + if_integral;
    
    // 限幅
    if (duty > 0.95f) duty = 0.95f;
    if (duty < 0.05f) duty = 0.05f;
    
    return duty;
}
