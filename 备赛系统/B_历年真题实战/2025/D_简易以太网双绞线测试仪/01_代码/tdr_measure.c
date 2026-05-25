/**
 * 2025D TDR时域反射测距模块
 * 
 * 原理：
 *   1. 向线缆发送窄脉冲
 *   2. 脉冲在线缆中传播，遇到终端（开路/短路）产生反射
 *   3. 测量发射到反射的时间差 dt
 *   4. 线缆长度 L = v * dt / 2
 * 
 * 双绞线传播速度约 v = 0.66c = 1.98×10^8 m/s
 * 1m线缆往返时间 = 2 / (1.98×10^8) ≈ 10.1ns
 * 
 * 时间测量方案：
 *   方案A: TDC芯片(TDC7200) - 精度25ps，推荐
 *   方案B: 高速比较器+定时器捕获 - 精度约10ns
 *   方案C: 高速ADC采样 - 需要≥100MSPS
 */

#include "stm32f4xx_hal.h"

/* ========== 参数 ========== */
// 双绞线传播速度 (需要用已知长度线缆校准)
static float propagation_velocity = 1.98e8f;  // m/s, 初始值

// TDC7200相关定义 (SPI接口)
#define TDC_CS_PORT   GPIOA
#define TDC_CS_PIN    GPIO_PIN_4
#define TDC_START_PORT GPIOA
#define TDC_START_PIN  GPIO_PIN_3
#define TDC_STOP_PORT  GPIOA  
#define TDC_STOP_PIN   GPIO_PIN_2

extern SPI_HandleTypeDef hspi1;

/* ========== TDC7200驱动 ========== */

static void TDC_WriteReg(uint8_t addr, uint8_t val)
{
    uint8_t buf[2] = {0x40 | addr, val};  // 写命令
    HAL_GPIO_WritePin(TDC_CS_PORT, TDC_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, buf, 2, 10);
    HAL_GPIO_WritePin(TDC_CS_PORT, TDC_CS_PIN, GPIO_PIN_SET);
}

static uint32_t TDC_ReadReg24(uint8_t addr)
{
    uint8_t tx[4] = {addr, 0, 0, 0};
    uint8_t rx[4];
    HAL_GPIO_WritePin(TDC_CS_PORT, TDC_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 4, 10);
    HAL_GPIO_WritePin(TDC_CS_PORT, TDC_CS_PIN, GPIO_PIN_SET);
    return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

void TDC_Init(void)
{
    // TDC7200初始化
    TDC_WriteReg(0x00, 0x03);  // CONFIG1: 测量模式2, 开始测量
    TDC_WriteReg(0x01, 0x40);  // CONFIG2: 校准周期
}

/**
 * 测量脉冲往返时间
 * @return 时间 (秒)，-1表示测量失败
 */
float TDC_MeasureTime(void)
{
    // 1. 启动测量
    TDC_WriteReg(0x00, 0x01);  // 开始测量
    
    // 2. 发送脉冲 (GPIO产生窄脉冲)
    HAL_GPIO_WritePin(TDC_START_PORT, TDC_START_PIN, GPIO_PIN_SET);
    // 约50ns脉冲宽度 (几个NOP指令)
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(TDC_START_PORT, TDC_START_PIN, GPIO_PIN_RESET);
    
    // 3. 等待测量完成 (反射信号触发STOP)
    HAL_Delay(5);  // 最长等待5ms
    
    // 4. 读取结果
    uint32_t time_result = TDC_ReadReg24(0x10);  // TIME1寄存器
    uint32_t cal1 = TDC_ReadReg24(0x1B);
    uint32_t cal2 = TDC_ReadReg24(0x1C);
    
    if (time_result == 0 || cal1 == 0) return -1;  // 测量失败
    
    // 5. 计算实际时间
    // TDC7200时间计算公式 (参考数据手册)
    float cal_count = (float)(cal2 - cal1) / 9.0f;  // 10个校准周期
    float time_ns = (float)time_result / cal_count * (1000.0f / 8.0f);  // 8MHz参考时钟
    
    return time_ns * 1e-9f;  // 转换为秒
}

/* ========== 长度测量 ========== */

/**
 * 测量线缆长度
 * @return 长度 (cm)，-1表示测量失败
 */
float TDR_MeasureLength(void)
{
    float total_time = 0;
    int valid_count = 0;
    
    // 多次测量取平均
    for (int i = 0; i < 10; i++) {
        float t = TDC_MeasureTime();
        if (t > 0 && t < 1e-6f) {  // 有效范围: 0~1μs (对应0~100m)
            total_time += t;
            valid_count++;
        }
        HAL_Delay(10);
    }
    
    if (valid_count == 0) return -1;
    
    float avg_time = total_time / valid_count;
    
    // 长度 = 速度 × 时间 / 2 (往返)
    float length_m = propagation_velocity * avg_time / 2.0f;
    
    return length_m * 100.0f;  // 转换为cm
}

/**
 * 校准传播速度
 * 用已知长度的线缆校准
 * @param known_length_cm 已知线缆长度 (cm)
 */
void TDR_Calibrate(float known_length_cm)
{
    float t = 0;
    int count = 0;
    
    for (int i = 0; i < 20; i++) {
        float ti = TDC_MeasureTime();
        if (ti > 0) {
            t += ti;
            count++;
        }
        HAL_Delay(10);
    }
    
    if (count > 0) {
        float avg_t = t / count;
        // v = 2L / t
        propagation_velocity = 2.0f * (known_length_cm / 100.0f) / avg_t;
    }
}

/* ========== 直流电阻测量 ========== */

/**
 * 测量线对直流电阻（双端模式）
 * 恒流源法：施加已知电流I，测量电压V，R=V/I
 * 
 * 电路：
 *   恒流源(1mA) → 线对一端 → 线缆 → 线对另一端 → 采样电阻 → GND
 *   ADC测量线对两端电压差
 */

#define CONST_CURRENT   0.001f   // 恒流源电流 1mA
#define ADC_VREF        3.3f
#define ADC_RESOLUTION  4096

float TDR_MeasureResistance(uint32_t adc_channel)
{
    // 读取ADC值
    // (实际需要配置ADC通道，这里简化)
    uint16_t adc_val = 0;  // = ADC_Read(adc_channel);
    
    float voltage = (float)adc_val / ADC_RESOLUTION * ADC_VREF;
    float resistance = voltage / CONST_CURRENT;
    
    return resistance;  // 单位: Ω
}
