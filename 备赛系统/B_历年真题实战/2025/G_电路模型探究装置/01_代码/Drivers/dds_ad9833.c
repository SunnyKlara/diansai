/**
 * AD9833 DDS驱动 —— 实现
 * 
 * ============================================================
 * 【AD9833的通信协议】
 * ============================================================
 * 
 * AD9833使用3线SPI，但和标准SPI有区别：
 * 1. 数据在SCLK下降沿锁存（CPOL=1, CPHA=0，即SPI Mode 2）
 * 2. FSYNC是低有效的片选，但不是标准的NSS
 *    FSYNC拉低→开始传输，FSYNC拉高→数据锁存
 * 3. 每次传输16bit（一个控制字）
 * 
 * 设置频率需要写3个控制字：
 *   1. 控制寄存器（选择频率寄存器、波形等）
 *   2. 频率寄存器低14位
 *   3. 频率寄存器高14位
 * 
 * ============================================================
 * 【为什么不用HAL_SPI_Transmit？】
 * ============================================================
 * 
 * HAL库的SPI函数有较大的开销（参数检查、状态机等），
 * 对于AD9833这种简单的3字节传输，用寄存器直接操作更快更可靠。
 * 
 * 但为了代码可读性，我还是用HAL库，只是在关键路径上做了优化。
 * 如果发现SPI通信不稳定，可以切换到寄存器操作版本。
 * 
 * ============================================================
 */

#include "dds_ad9833.h"
#include "../config.h"

static SPI_HandleTypeDef* p_hspi = NULL;

/* -------- AD9833寄存器定义 -------- */

// 控制寄存器各bit的含义
#define AD9833_CTRL_B28     (1 << 13)   // 1=28bit频率寄存器一次写入
#define AD9833_CTRL_HLB     (1 << 12)   // 0=写低14bit, 1=写高14bit（B28=0时用）
#define AD9833_CTRL_FSEL    (1 << 11)   // 频率寄存器选择 0=FREQ0, 1=FREQ1
#define AD9833_CTRL_PSEL    (1 << 10)   // 相位寄存器选择
#define AD9833_CTRL_RESET   (1 << 8)    // 1=复位（输出归零）
#define AD9833_CTRL_SLEEP1  (1 << 7)    // 1=关闭内部时钟
#define AD9833_CTRL_SLEEP12 (1 << 6)    // 1=关闭DAC
#define AD9833_CTRL_OPBITEN (1 << 5)    // 1=输出MSB（方波模式）
#define AD9833_CTRL_DIV2    (1 << 3)    // 1=MSB/2输出
#define AD9833_CTRL_MODE    (1 << 1)    // 1=三角波, 0=正弦波

// 频率寄存器地址
#define AD9833_FREQ0_REG    (1 << 14)   // FREQ0寄存器标识 (bit15:14 = 01)
#define AD9833_FREQ1_REG    (1 << 15)   // FREQ1寄存器标识 (bit15:14 = 10)

// 相位寄存器地址
#define AD9833_PHASE0_REG   (3 << 14)   // PHASE0 (bit15:14 = 11, bit13 = 0)
#define AD9833_PHASE1_REG   ((3 << 14) | (1 << 13))  // PHASE1

/* -------- 底层SPI写入 -------- */

/**
 * 向AD9833写入一个16bit控制字
 * 
 * 时序：
 *   FSYNC ──┐     ┌──
 *            └─────┘
 *   SCLK  ──┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐ ┐
 *            └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘
 *   SDATA  D15 D14 D13 ... D1 D0
 *           ↑ 数据在SCLK下降沿锁存
 */
static void ad9833_write16(uint16_t data)
{
    // FSYNC拉低
    HAL_GPIO_WritePin(DDS_FSYNC_PORT, DDS_FSYNC_PIN, GPIO_PIN_RESET);
    
    // 发送16bit（MSB first）
    // AD9833要求SPI Mode 2 (CPOL=1, CPHA=0)
    // STM32的HAL库SPI配置需要匹配
    uint8_t buf[2];
    buf[0] = (data >> 8) & 0xFF;   // 高字节先发
    buf[1] = data & 0xFF;           // 低字节后发
    
    HAL_SPI_Transmit(p_hspi, buf, 2, 10);
    
    // FSYNC拉高（数据锁存）
    HAL_GPIO_WritePin(DDS_FSYNC_PORT, DDS_FSYNC_PIN, GPIO_PIN_SET);
}

/* -------- 对外接口实现 -------- */

void DDS_Init(SPI_HandleTypeDef* hspi)
{
    p_hspi = hspi;
    
    // FSYNC初始为高（不选中）
    HAL_GPIO_WritePin(DDS_FSYNC_PORT, DDS_FSYNC_PIN, GPIO_PIN_SET);
    
    // 写入复位命令
    ad9833_write16(AD9833_CTRL_RESET);
    
    // 等待复位完成
    HAL_Delay(1);
}

void DDS_SetFreq(float freq_hz)
{
    /*
     * 频率寄存器值 = freq × 2^28 / MCLK
     * 
     * 用整数运算避免浮点精度问题：
     *   freq_reg = (uint32_t)(freq_hz * 268435456.0 / DDS_MCLK_FREQ)
     *   
     * 但268435456.0 * freq_hz可能溢出float的精度（float只有24bit有效位）
     * 所以分两步算：
     *   freq_reg = (uint32_t)((double)freq_hz * 268435456.0 / DDS_MCLK_FREQ)
     * 
     * 或者用定点数：
     *   freq_reg = (uint32_t)(freq_hz / DDS_MCLK_FREQ * 268435456.0)
     *   先除后乘，避免中间结果溢出
     */
    
    // 使用double确保精度
    double freq_reg_d = (double)freq_hz * 268435456.0 / (double)DDS_MCLK_FREQ;
    uint32_t freq_reg = (uint32_t)(freq_reg_d + 0.5);  // 四舍五入
    
    // 限幅（28bit最大值）
    if (freq_reg > 0x0FFFFFFF) freq_reg = 0x0FFFFFFF;
    
    // 拆分为低14位和高14位
    uint16_t freq_low  = (uint16_t)(freq_reg & 0x3FFF);
    uint16_t freq_high = (uint16_t)((freq_reg >> 14) & 0x3FFF);
    
    // 写入控制寄存器：B28=1（28bit模式），RESET=1（保持复位直到全部写完）
    ad9833_write16(AD9833_CTRL_B28 | AD9833_CTRL_RESET);
    
    // 写入FREQ0低14位
    ad9833_write16(AD9833_FREQ0_REG | freq_low);
    
    // 写入FREQ0高14位
    ad9833_write16(AD9833_FREQ0_REG | freq_high);
    
    // 注意：此时仍在复位状态，需要调用DDS_Start()才会输出
}

void DDS_SetWaveform(DDS_Waveform_t wave)
{
    uint16_t ctrl = AD9833_CTRL_B28;  // 保持B28=1
    
    switch (wave) {
        case DDS_WAVE_SINE:
            // 默认就是正弦波，不需要额外bit
            break;
            
        case DDS_WAVE_TRIANGLE:
            ctrl |= AD9833_CTRL_MODE;
            break;
            
        case DDS_WAVE_SQUARE:
            ctrl |= AD9833_CTRL_OPBITEN | AD9833_CTRL_DIV2;
            break;
    }
    
    ad9833_write16(ctrl);
}

void DDS_Start(void)
{
    // 清除RESET位，开始输出
    // 保持B28=1，波形设置保持不变
    ad9833_write16(AD9833_CTRL_B28);
}

void DDS_Stop(void)
{
    // 设置RESET位，输出归零
    ad9833_write16(AD9833_CTRL_B28 | AD9833_CTRL_RESET);
}

void DDS_SetPhase(float phase_deg)
{
    // 相位寄存器是12bit，对应0~2π
    // phase_reg = phase_deg / 360 × 4096
    uint16_t phase_reg = (uint16_t)(phase_deg / 360.0f * 4096.0f);
    phase_reg &= 0x0FFF;  // 12bit
    
    ad9833_write16(AD9833_PHASE0_REG | phase_reg);
}
