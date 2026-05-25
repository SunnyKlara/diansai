/**
 * 2025D 简易以太网双绞线测试仪 - 主程序
 * MCU: STM32F407VET6
 * 
 * 功能：
 *   - 双端检测：线序(直连/交叉)、类型(UTP/SFTP)、直流电阻
 *   - 单端检测：长度(TDR)、短路检测/定位
 *   - LCD显示测量结果
 *   - 按键切换模式
 */

#include "stm32f4xx_hal.h"
#include "wire_test.h"
#include "tdr_measure.h"
#include <stdio.h>
#include <string.h>

/* ========== 工作模式 ========== */
typedef enum {
    MODE_DUAL_END,   // 双端检测
    MODE_SINGLE_END  // 单端检测
} WorkMode_t;

static WorkMode_t current_mode = MODE_DUAL_END;

/* ========== LCD显示（简化接口） ========== */
extern void LCD_Init(void);
extern void LCD_Clear(uint16_t color);
extern void LCD_ShowString(uint16_t x, uint16_t y, const char* str, uint16_t color);

void display_mode(void)
{
    LCD_Clear(0x0000);  // 黑色背景
    if (current_mode == MODE_DUAL_END) {
        LCD_ShowString(10, 10, "MODE: DUAL-END", 0xFFFF);
        LCD_ShowString(10, 30, "Press START to test", 0x07E0);
    } else {
        LCD_ShowString(10, 10, "MODE: SINGLE-END", 0xFFFF);
        LCD_ShowString(10, 30, "Press START to test", 0x07E0);
    }
}

void display_dual_result(WireType_t wire, CableType_t cable, float resistance)
{
    char buf[32];
    
    LCD_Clear(0x0000);
    LCD_ShowString(10, 10, "=== DUAL-END ===", 0xFFE0);
    
    // 线序
    switch (wire) {
        case WIRE_STRAIGHT:  LCD_ShowString(10, 40, "Wire: STRAIGHT", 0x07E0); break;
        case WIRE_CROSSOVER: LCD_ShowString(10, 40, "Wire: CROSSOVER", 0x07E0); break;
        default:             LCD_ShowString(10, 40, "Wire: FAULT!", 0xF800); break;
    }
    
    // 类型
    if (cable == CABLE_SFTP) {
        LCD_ShowString(10, 60, "Type: SFTP", 0x07E0);
    } else {
        LCD_ShowString(10, 60, "Type: UTP", 0x07E0);
    }
    
    // 电阻
    snprintf(buf, sizeof(buf), "R: %.2f Ohm", resistance);
    LCD_ShowString(10, 80, buf, 0x07E0);
    
    LCD_ShowString(10, 110, "Status: RESULT HOLD", 0xFFFF);
}

void display_single_result(float length_cm, uint8_t has_short, float short_pos_cm)
{
    char buf[32];
    
    LCD_Clear(0x0000);
    LCD_ShowString(10, 10, "=== SINGLE-END ===", 0xFFE0);
    
    // 长度
    snprintf(buf, sizeof(buf), "Length: %.1f cm", length_cm);
    LCD_ShowString(10, 40, buf, 0x07E0);
    
    // 短路检测
    if (has_short) {
        LCD_ShowString(10, 60, "Short: DETECTED!", 0xF800);
        snprintf(buf, sizeof(buf), "Pos: %.1f cm", short_pos_cm);
        LCD_ShowString(10, 80, buf, 0xF800);
    } else {
        LCD_ShowString(10, 60, "Short: NONE", 0x07E0);
    }
    
    LCD_ShowString(10, 110, "Status: RESULT HOLD", 0xFFFF);
}

/* ========== 按键处理 ========== */
// PA8: 模式切换按键
// PA9: 启动测量按键

void check_buttons(void)
{
    // 模式切换
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        current_mode = (current_mode == MODE_DUAL_END) ? MODE_SINGLE_END : MODE_DUAL_END;
        display_mode();
    }
    
    // 启动测量
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_RESET) {
        HAL_Delay(200);
        
        // 显示"正在检测"
        LCD_Clear(0x0000);
        LCD_ShowString(10, 50, "TESTING...", 0xFFE0);
        
        if (current_mode == MODE_DUAL_END) {
            // 双端检测
            WireType_t wire = WireTest_CheckMapping();
            CableType_t cable = WireTest_CheckCableType();
            float resistance = TDR_MeasureResistance(ADC_CHANNEL_0);
            
            display_dual_result(wire, cable, resistance);
            
        } else {
            // 单端检测
            float length = TDR_MeasureLength();
            uint8_t has_short = WireTest_CheckShort();
            float short_pos = 0;
            if (has_short) {
                short_pos = TDR_MeasureLength();  // 短路时TDR测到短路点
            }
            
            display_single_result(length, has_short, short_pos);
        }
    }
}

/* ========== 主程序 ========== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    
    MX_GPIO_Init();
    MX_SPI1_Init();    // TDC7200
    MX_SPI2_Init();    // LCD
    MX_ADC1_Init();    // 电阻测量
    
    LCD_Init();
    TDC_Init();
    
    // 开机校准提示
    LCD_Clear(0x0000);
    LCD_ShowString(10, 10, "Cable Tester v1.0", 0xFFFF);
    LCD_ShowString(10, 40, "Connect 1m SFTP", 0xFFE0);
    LCD_ShowString(10, 60, "for calibration", 0xFFE0);
    LCD_ShowString(10, 90, "Press START", 0x07E0);
    
    // 等待校准
    while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET) {
        HAL_Delay(10);
    }
    HAL_Delay(200);
    
    // 用1m线缆校准
    LCD_ShowString(10, 110, "Calibrating...", 0xFFE0);
    TDR_Calibrate(100.0f);  // 100cm
    
    LCD_ShowString(10, 130, "Done! Remove cable", 0x07E0);
    HAL_Delay(2000);
    
    // 进入正常工作模式
    display_mode();
    
    while (1) {
        check_buttons();
        HAL_Delay(50);
    }
}
