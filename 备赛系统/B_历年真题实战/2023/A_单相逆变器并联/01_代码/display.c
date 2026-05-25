/**
 * OLED显示模块 (SSD1306, 128x64, I2C)
 * 
 * 简化版驱动，只实现电赛需要的功能：
 *   - 显示字符串
 *   - 显示浮点数（带标签和单位）
 * 
 * 完整SSD1306驱动代码较长，这里给出接口封装
 * 实际使用时可以用现成的SSD1306库（如u8g2或自己移植的）
 */

#include "display.h"
#include <stdio.h>
#include <string.h>

/* ===== SSD1306底层驱动（简化版） ===== */

static I2C_HandleTypeDef* p_hi2c;
#define SSD1306_ADDR  0x78  // I2C地址 (7bit: 0x3C)

static void SSD1306_WriteCmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(p_hi2c, SSD1306_ADDR, buf, 2, 10);
}

static void SSD1306_WriteData(uint8_t* data, uint16_t len)
{
    // 实际实现需要加0x40前缀
    // 这里省略完整实现，使用时替换为完整SSD1306库
}

/* ===== 字库（6x8 ASCII） ===== */
// 实际使用时包含完整字库文件 font6x8.h
// extern const uint8_t Font6x8[][6];

/* ===== 对外接口 ===== */

void Display_Init(I2C_HandleTypeDef* hi2c)
{
    p_hi2c = hi2c;
    
    // SSD1306初始化序列
    HAL_Delay(100);  // 等待上电稳定
    SSD1306_WriteCmd(0xAE); // 关显示
    SSD1306_WriteCmd(0xD5); // 设置时钟分频
    SSD1306_WriteCmd(0x80);
    SSD1306_WriteCmd(0xA8); // 设置多路复用率
    SSD1306_WriteCmd(0x3F); // 64行
    SSD1306_WriteCmd(0xD3); // 设置显示偏移
    SSD1306_WriteCmd(0x00);
    SSD1306_WriteCmd(0x40); // 设置起始行
    SSD1306_WriteCmd(0x8D); // 电荷泵
    SSD1306_WriteCmd(0x14); // 开启
    SSD1306_WriteCmd(0xA1); // 段重映射
    SSD1306_WriteCmd(0xC8); // COM扫描方向
    SSD1306_WriteCmd(0xDA); // COM引脚配置
    SSD1306_WriteCmd(0x12);
    SSD1306_WriteCmd(0x81); // 对比度
    SSD1306_WriteCmd(0xCF);
    SSD1306_WriteCmd(0xD9); // 预充电周期
    SSD1306_WriteCmd(0xF1);
    SSD1306_WriteCmd(0xDB); // VCOMH电压
    SSD1306_WriteCmd(0x40);
    SSD1306_WriteCmd(0xA4); // 全局显示开启
    SSD1306_WriteCmd(0xA6); // 正常显示（非反色）
    SSD1306_WriteCmd(0xAF); // 开显示
    
    Display_Clear();
}

void Display_Clear(void)
{
    // 清除显存（全写0）
    for (uint8_t page = 0; page < 8; page++) {
        SSD1306_WriteCmd(0xB0 + page);  // 设置页地址
        SSD1306_WriteCmd(0x00);          // 列地址低4位
        SSD1306_WriteCmd(0x10);          // 列地址高4位
        uint8_t zeros[128];
        memset(zeros, 0, 128);
        SSD1306_WriteData(zeros, 128);
    }
}

void Display_ShowString(uint8_t x, uint8_t y, const char* str)
{
    // 在(x, y)位置显示字符串
    // x: 像素列(0~127), y: 页(0~7)
    // 实际实现需要查字库逐字符写入
    // 使用时替换为完整实现
    (void)x; (void)y; (void)str;
}

/**
 * 显示浮点数（电赛最常用的显示格式）
 * 例: Display_ShowFloat(0, 2, "Vout:", 24.01, "V")
 * 显示: "Vout: 24.01V"
 */
void Display_ShowFloat(uint8_t x, uint8_t y, const char* label, float val, const char* unit)
{
    char buf[22];
    snprintf(buf, sizeof(buf), "%s%6.2f%s", label, val, unit);
    Display_ShowString(x, y, buf);
}
