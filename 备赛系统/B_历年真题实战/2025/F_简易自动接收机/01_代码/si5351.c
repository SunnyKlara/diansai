/**
 * Si5351A 驱动 - 简化版
 * 
 * Si5351的频率设置涉及PLL和MultiSynth两级分频：
 *   fVCO = fXTAL × (a + b/c)     PLL倍频
 *   fOUT = fVCO / (d + e/f)       MultiSynth分频
 * 
 * 简化方案：固定PLL频率为900MHz，只调MultiSynth分频
 *   fOUT = 900MHz / divider
 *   divider = 900000000 / fOUT
 * 
 * 适用范围：约5.6MHz ~ 112.5MHz（divider 8~160）
 * 对于88~108MHz+10.7MHz=98.7~118.7MHz的本振需求，
 * 需要PLL频率设高一些或用分数分频
 */

#include "si5351.h"

extern I2C_HandleTypeDef hi2c1;

static void si5351_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    HAL_I2C_Master_Transmit(&hi2c1, SI5351_ADDR, buf, 2, 10);
}

static uint8_t si5351_read(uint8_t reg)
{
    uint8_t val;
    HAL_I2C_Master_Transmit(&hi2c1, SI5351_ADDR, &reg, 1, 10);
    HAL_I2C_Master_Receive(&hi2c1, SI5351_ADDR, &val, 1, 10);
    return val;
}

void Si5351_Init(void)
{
    // 禁用所有输出
    si5351_write(3, 0xFF);
    
    // 设置晶振负载电容
    si5351_write(183, 0xC0);  // 10pF
    
    // 设置PLL A: 900MHz (25MHz × 36)
    // a=36, b=0, c=1
    // P1 = 128*36 - 512 = 4096
    // P2 = 0, P3 = 1
    si5351_write(26, 0x00);  // MSNA_P3[15:8]
    si5351_write(27, 0x01);  // MSNA_P3[7:0]
    si5351_write(28, 0x00);  // MSNA_P1[17:16]
    si5351_write(29, 0x10);  // MSNA_P1[15:8] = 16
    si5351_write(30, 0x00);  // MSNA_P1[7:0] = 0
    si5351_write(31, 0x00);  // MSNA_P2[19:16]
    si5351_write(32, 0x00);  // MSNA_P2[15:8]
    si5351_write(33, 0x00);  // MSNA_P2[7:0]
    
    // PLL复位
    si5351_write(177, 0xA0);
    
    // 使能输出
    si5351_write(3, 0x00);
}

/**
 * 设置CLK0输出频率
 * 使用整数分频模式
 */
void Si5351_SetFreq(uint8_t clk_num, uint32_t freq_hz)
{
    if (freq_hz == 0) return;
    
    // 计算分频比 = 900MHz / freq
    // 使用分数分频: divider = a + b/c
    uint32_t pll_freq = 900000000UL;
    uint32_t a = pll_freq / freq_hz;
    uint32_t remainder = pll_freq - a * freq_hz;
    uint32_t c = freq_hz;  // 简化：c=freq
    uint32_t b = remainder;
    
    // 简化分数 (GCD)
    // 为了简化，限制c的最大值
    while (c > 1048575) {  // 20bit限制
        b >>= 1;
        c >>= 1;
    }
    
    // 计算寄存器值
    uint32_t P1 = 128 * a + (128 * b / c) - 512;
    uint32_t P2 = 128 * b - c * (128 * b / c);
    uint32_t P3 = c;
    
    // 写入MultiSynth寄存器 (CLK0: reg 42~49)
    uint8_t base_reg = 42 + clk_num * 8;
    
    si5351_write(base_reg + 0, (P3 >> 8) & 0xFF);
    si5351_write(base_reg + 1, P3 & 0xFF);
    si5351_write(base_reg + 2, ((P1 >> 16) & 0x03));
    si5351_write(base_reg + 3, (P1 >> 8) & 0xFF);
    si5351_write(base_reg + 4, P1 & 0xFF);
    si5351_write(base_reg + 5, ((P3 >> 12) & 0xF0) | ((P2 >> 16) & 0x0F));
    si5351_write(base_reg + 6, (P2 >> 8) & 0xFF);
    si5351_write(base_reg + 7, P2 & 0xFF);
    
    // CLK控制寄存器
    si5351_write(16 + clk_num, 0x4F);  // PLL A, 8mA驱动
}

void Si5351_EnableOutput(uint8_t clk_num, uint8_t enable)
{
    uint8_t val = si5351_read(3);
    if (enable) {
        val &= ~(1 << clk_num);
    } else {
        val |= (1 << clk_num);
    }
    si5351_write(3, val);
}
