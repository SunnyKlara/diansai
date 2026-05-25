/**
 * 2023B TDR时域反射核心算法
 * 
 * ============================================================
 * 【TDR为什么连续两年出题？(2023B + 2025D)】
 * ============================================================
 * 
 * TDR是一种优雅的测量技术：
 * 只需要在电缆一端发送脉冲，就能知道电缆的长度、
 * 终端是开路还是短路、接了什么负载、甚至故障点在哪里。
 * 不需要接触电缆的另一端！
 * 
 * 这在工程中极其实用（网络布线检测、电力线路故障定位），
 * 而且涉及的技术（高速脉冲、精密时间测量、信号处理）
 * 恰好是电赛想考察的能力。
 * 
 * ============================================================
 * 【2023B vs 2025D的区别】
 * ============================================================
 * 
 * 2023B：同轴电缆（50Ω单端），测长度+负载类型+负载参数
 * 2025D：双绞线（100Ω差分），测长度+线序+类型+电阻+衰减+短路位置
 * 
 * 2023B更侧重负载识别（电阻/电容），2025D更侧重综合测量。
 * TDR核心算法是相同的，掌握2023B就能做2025D。
 * 
 * ============================================================
 * 【TDR的物理本质】
 * ============================================================
 * 
 * 电磁波在传输线中传播，遇到阻抗不连续点会产生反射。
 * 反射系数 ρ = (ZL - Z0) / (ZL + Z0)
 * 
 * Z0 = 电缆特性阻抗（同轴电缆通常50Ω）
 * ZL = 终端负载阻抗
 * 
 * 开路：ZL=∞ → ρ=+1（正反射，反射波与入射波同相）
 * 短路：ZL=0 → ρ=-1（负反射，反射波与入射波反相）
 * 匹配：ZL=Z0 → ρ=0（无反射）
 * 电阻：ZL=R → ρ=(R-50)/(R+50)
 * 电容：ZL=1/(jωC) → 反射波有RC充放电特征
 * 
 * ============================================================
 */

#include "stm32f4xx_hal.h"
#include <math.h>

#define Z0 50.0f  // 同轴电缆特性阻抗 (Ω)

/* ========== 时间测量方案选择 ========== */

/**
 * 【三种时间测量方案的深度对比】
 * 
 * 方案A：高速ADC直接采样
 *   用100MSPS ADC采样反射波形
 *   时间分辨率 = 10ns → 距离分辨率 = 1m
 *   优点：能看到完整的反射波形（可以分析负载类型）
 *   缺点：需要昂贵的高速ADC，数据量大
 *   
 * 方案B：TDC芯片（推荐）
 *   TDC7200：时间分辨率55ps → 距离分辨率 = 5.5mm
 *   只测量发射到反射的时间差，不记录波形
 *   优点：精度极高，成本低
 *   缺点：看不到反射波形，负载类型识别需要其他方法
 *   
 * 方案C：等效采样
 *   用普通ADC(1MSPS)，但每次采样延迟不同
 *   多次测量拼接出高时间分辨率的波形
 *   优点：不需要高速ADC
 *   缺点：需要多次测量，速度慢
 * 
 * 【我的选择】方案B(TDC7200) + 方案C(等效采样)的组合
 *   TDC7200测精确时间 → 计算长度
 *   等效采样看反射波形 → 识别负载类型
 */

/* ========== TDC7200精密时间测量 ========== */

// TDC7200 SPI驱动（简化版，完整版见si5351.c的风格）
extern void TDC_WriteReg(uint8_t addr, uint8_t val);
extern uint32_t TDC_ReadReg24(uint8_t addr);

/**
 * 发送TDR脉冲并测量反射时间
 * 
 * 【脉冲宽度的选择】
 * 
 * 脉冲越窄 → 距离分辨率越高（能区分更近的反射点）
 * 但脉冲越窄 → 能量越低 → 反射信号越弱 → 检测越困难
 * 
 * 对于1000~2000cm的电缆：
 *   往返时间 = 2×20m / (0.66×3×10^8) = 202ns
 *   脉冲宽度应该 << 202ns，否则发射脉冲和反射脉冲会重叠
 *   选择脉冲宽度 = 10~20ns
 *   
 * 对于发挥部分的100cm盲区：
 *   往返时间 = 2×1m / (0.66×3×10^8) = 10.1ns
 *   脉冲宽度必须 < 10ns！
 *   STM32 GPIO翻转速度约5ns@168MHz → 勉强可以
 *   更好的方案：用74AC04反相器链产生更窄的脉冲
 */

// 脉冲发射引脚
#define PULSE_PORT GPIOA
#define PULSE_PIN  GPIO_PIN_3

void send_pulse(void)
{
    // 产生约10ns的窄脉冲
    // 方法：GPIO置高→几个NOP→置低
    PULSE_PORT->BSRR = PULSE_PIN;       // 置高
    __NOP(); __NOP(); __NOP();            // 约18ns@168MHz
    PULSE_PORT->BSRR = PULSE_PIN << 16;  // 置低
}

/**
 * 测量电缆长度
 * 
 * @param v_prop  传播速度(m/s)，需要校准
 * @return 长度(cm)，-1表示测量失败
 */
float tdr_measure_length(float v_prop)
{
    float total_time = 0;
    int valid = 0;
    
    for (int i = 0; i < 20; i++) {  // 20次测量取平均
        // 启动TDC测量
        TDC_WriteReg(0x00, 0x01);
        
        // 发送脉冲（触发TDC START）
        send_pulse();
        
        // 等待反射（TDC STOP由比较器触发）
        HAL_Delay(1);
        
        // 读取时间
        uint32_t time_raw = TDC_ReadReg24(0x10);
        if (time_raw > 0 && time_raw < 0xFFFFFF) {
            // 转换为秒（TDC7200的时间计算，参考数据手册）
            float time_ns = (float)time_raw * 0.055f;  // 55ps分辨率
            total_time += time_ns;
            valid++;
        }
        
        HAL_Delay(5);
    }
    
    if (valid < 5) return -1;  // 有效测量太少
    
    float avg_time_ns = total_time / valid;
    float length_m = v_prop * avg_time_ns * 1e-9f / 2.0f;  // 除以2（往返）
    
    return length_m * 100.0f;  // 转换为cm
}

/* ========== 负载类型识别 ========== */

/**
 * 【负载识别的核心思路】
 * 
 * 不同负载的反射波形不同：
 * 
 * 开路(ZL=∞)：
 *   反射波 = +入射波（同相同幅）
 *   反射系数 ρ = +1
 *   
 * 电阻(ZL=R)：
 *   反射波 = ρ × 入射波
 *   ρ = (R-50)/(R+50)
 *   R>50: ρ>0（正反射）
 *   R<50: ρ<0（负反射）
 *   反射波形状与入射波相同，只是幅度不同
 *   
 * 电容(ZL=1/jωC)：
 *   反射波有RC充放电特征
 *   初始瞬间：电容相当于短路 → ρ=-1
 *   稳态：电容相当于开路 → ρ=+1
 *   反射波从-1指数上升到+1
 *   时间常数 τ = Z0 × C = 50 × C
 *   
 * 【区分方法】
 * 用等效采样获取反射波形，分析波形特征：
 * - 反射波形状与入射波相同 → 电阻
 * - 反射波有指数上升特征 → 电容
 * - 无反射 → 匹配负载（不太可能出现在题目中）
 */

typedef enum {
    LOAD_OPEN,       // 开路
    LOAD_SHORT,      // 短路
    LOAD_RESISTOR,   // 电阻
    LOAD_CAPACITOR,  // 电容
    LOAD_UNKNOWN
} LoadType_t;

typedef struct {
    LoadType_t type;
    float resistance;   // 电阻值(Ω)，仅type=LOAD_RESISTOR时有效
    float capacitance;  // 电容值(F)，仅type=LOAD_CAPACITOR时有效
} LoadResult_t;

/**
 * 识别负载类型和参数
 * 
 * 方法：测量反射系数ρ
 *   ρ ≈ +1 → 开路
 *   ρ ≈ -1 → 短路
 *   |ρ| < 0.9 且波形为阶跃 → 电阻，R = Z0×(1+ρ)/(1-ρ)
 *   波形有指数特征 → 电容
 * 
 * 简化实现：用ADC测量反射波的峰值和稳态值
 */
LoadResult_t identify_load(void)
{
    LoadResult_t result = {LOAD_UNKNOWN, 0, 0};
    
    // 发送脉冲，用ADC采样反射波
    // （需要高速ADC或等效采样，这里用简化方法）
    
    // 测量反射波的峰值电压（相对于入射波）
    // 实际实现需要精确的模拟前端
    float v_incident = 1.0f;  // 入射波幅度（归一化）
    float v_reflected = 0;     // 反射波幅度（需要ADC测量）
    
    // ... ADC测量代码 ...
    
    float rho = v_reflected / v_incident;
    
    if (rho > 0.9f) {
        result.type = LOAD_OPEN;
    } else if (rho < -0.9f) {
        result.type = LOAD_SHORT;
    } else if (fabsf(rho) < 0.9f) {
        // 可能是电阻或电容
        // 需要进一步分析波形形状
        
        // 简化判断：如果反射波在100ns内稳定 → 电阻
        //           如果反射波有缓慢变化 → 电容
        
        // 假设已经判断为电阻
        result.type = LOAD_RESISTOR;
        result.resistance = Z0 * (1.0f + rho) / (1.0f - rho);
    }
    
    return result;
}

/* ========== 传播速度校准 ========== */

/**
 * 【校准是TDR精度的关键】
 * 
 * 传播速度 v = c / √εr
 * 不同电缆的εr不同：
 *   RG58 (PE绝缘): εr=2.3, v=0.66c
 *   RG59 (PE绝缘): εr=2.3, v=0.66c
 *   RG174 (PE绝缘): εr=2.3, v=0.66c
 *   RG6 (泡沫PE): εr≈1.5, v=0.82c
 * 
 * 即使同一型号，不同批次的εr也有差异。
 * 所以必须用已知长度的电缆校准！
 * 
 * 校准方法：
 *   1. 接上已知长度L_known的电缆（终端开路）
 *   2. 测量往返时间t
 *   3. v = 2 × L_known / t
 */
static float calibrated_velocity = 1.98e8f;  // 默认0.66c

void tdr_calibrate(float known_length_cm)
{
    float t_total = 0;
    int count = 0;
    
    for (int i = 0; i < 50; i++) {
        float length = tdr_measure_length(calibrated_velocity);
        if (length > 0) {
            // 反算速度
            // length_measured = v × t / 2
            // 我们知道真实长度，所以 v_true = v_current × known / measured
            float ratio = known_length_cm / length;
            t_total += ratio;
            count++;
        }
        HAL_Delay(10);
    }
    
    if (count > 10) {
        float avg_ratio = t_total / count;
        calibrated_velocity *= avg_ratio;
    }
}
