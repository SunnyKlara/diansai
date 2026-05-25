/**
 * 闭环控制模块
 * 电压外环PI控制器：根据输出电压RMS值调节调制比
 */

#include "control.h"

/* PI控制器参数 - 需要实际调参 */
#define KP_V    0.02f     // 比例系数
#define KI_V    0.005f    // 积分系数
#define MOD_MAX 0.95f     // 调制比上限
#define MOD_MIN 0.10f     // 调制比下限

static float vout_ref = 24.0f;
static float integral_v = 0.0f;

void Control_Init(float vref)
{
    vout_ref = vref;
    integral_v = 0.7f;  // 初始积分值=预估调制比，加速启动
}

/**
 * 电压环PI控制器
 * @param vout_rms 当前输出电压有效值
 * @return 调制比 [MOD_MIN, MOD_MAX]
 * 
 * 调用频率：每个正弦周期一次 (50Hz = 20ms)
 */
float Control_VoltageLoop(float vout_rms)
{
    float error = vout_ref - vout_rms;
    
    // 积分项（带抗饱和）
    integral_v += KI_V * error;
    if (integral_v > MOD_MAX) integral_v = MOD_MAX;
    if (integral_v < MOD_MIN) integral_v = MOD_MIN;
    
    // PI输出
    float mod = KP_V * error + integral_v;
    
    // 限幅
    if (mod > MOD_MAX) mod = MOD_MAX;
    if (mod < MOD_MIN) mod = MOD_MIN;
    
    return mod;
}

void Control_SetVref(float vref)
{
    vout_ref = vref;
}
