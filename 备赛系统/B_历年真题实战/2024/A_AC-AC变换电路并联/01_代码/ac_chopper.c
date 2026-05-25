/**
 * 2024A AC-AC交流斩波器控制
 * MCU: STM32G474RET6
 * 
 * 原理：
 *   交流斩波器通过PWM斩波实现交流调压
 *   Vout = D × Vin (D为占空比)
 *   
 * 双向开关结构：
 *   使用4个MOSFET组成两组背靠背开关
 *   S1(正半周): Q1a-Q1b 背靠背(共源极)
 *   S2(负半周): Q2a-Q2b 背靠背(共源极)
 *   
 * 换流策略：四步换流法
 *   根据输入电压极性和输出电流方向决定换流顺序
 *   避免桥臂直通(短路)和开路(电感电流无回路)
 */

#include "stm32g4xx_hal.h"
#include <math.h>

/* ========== 参数 ========== */
#define FSW         20000    // 开关频率 20kHz
#define VIN_NOMINAL 36.0f    // 额定输入电压(Vrms)
#define VOUT_MIN    1.0f     // 最小输出电压
#define VOUT_MAX    35.0f    // 最大输出电压
#define VOUT_STEP   0.5f     // 步进0.5V

/* ========== 状态变量 ========== */
static float vout_setpoint = 24.0f;  // 输出电压设定值
static float duty = 0.667f;          // 占空比 = Vout/Vin
static float vin_rms = 36.0f;        // 输入电压有效值

/* ========== 电压极性检测 ========== */
// 通过过零检测电路获取输入电压极性
// PA11: 过零检测信号(高=正半周, 低=负半周)

uint8_t get_vin_polarity(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_SET ? 1 : 0;
}

/* ========== 四步换流法 ========== */
// 换流步骤(从S1导通切换到S2导通，正半周情况):
// Step1: 关断S1中不导通电流的管子
// Step2: 开通S2中将要导通电流的管子
// Step3: 关断S1中导通电流的管子
// Step4: 开通S2中另一个管子
// 每步之间需要死区延时(约1μs)

// 简化实现：利用STM32G4的HRTIM实现精确时序
// 或者用软件延时实现四步换流

/* ========== PWM更新 ========== */
extern TIM_HandleTypeDef htim1;

void ac_chopper_update(void)
{
    uint8_t polarity = get_vin_polarity();
    uint16_t arr = htim1.Instance->ARR;
    uint16_t ccr = (uint16_t)(duty * arr);
    
    if (polarity) {
        // 正半周：S1组PWM斩波，S2组关断
        htim1.Instance->CCR1 = ccr;   // S1a
        htim1.Instance->CCR2 = ccr;   // S1b
        htim1.Instance->CCR3 = 0;     // S2a关断
        htim1.Instance->CCR4 = 0;     // S2b关断
    } else {
        // 负半周：S2组PWM斩波，S1组关断
        htim1.Instance->CCR1 = 0;
        htim1.Instance->CCR2 = 0;
        htim1.Instance->CCR3 = ccr;
        htim1.Instance->CCR4 = ccr;
    }
}

/* ========== 电压闭环 ========== */
static float v_integral = 0;
#define KP_AC  0.01f
#define KI_AC  0.002f

void voltage_loop(float vout_rms_measured)
{
    float error = vout_setpoint - vout_rms_measured;
    
    v_integral += KI_AC * error;
    if (v_integral > 0.95f) v_integral = 0.95f;
    if (v_integral < 0.05f) v_integral = 0.05f;
    
    duty = KP_AC * error + v_integral;
    if (duty > 0.95f) duty = 0.95f;
    if (duty < 0.03f) duty = 0.03f;
}

/* ========== 设定输出电压 ========== */
void set_vout(float vout)
{
    if (vout < VOUT_MIN) vout = VOUT_MIN;
    if (vout > VOUT_MAX) vout = VOUT_MAX;
    
    // 量化到0.5V步进
    vout = ((int)(vout / VOUT_STEP + 0.5f)) * VOUT_STEP;
    
    vout_setpoint = vout;
    
    // 预设占空比(开环初值，加速闭环收敛)
    duty = vout / vin_rms;
    v_integral = duty;
}
