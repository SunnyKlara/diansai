/**
 * @file    oled.c
 * @brief   SSD1306 OLED 占位实现
 *
 *  TODO_TI：等 MSP432 SimpleLink SDK 配置好 I2C UCB0 后，
 *           填充 i2c_write_cmd / i2c_write_data 即可。
 *           完整字模可从开源仓库（如 oled_u8g2）粘贴。
 */

#include "oled.h"

/* TODO_TI: #include <ti/devices/msp432p4xx/driverlib/driverlib.h> */

void OLED_Init(void)              { /* TODO_TI */ }
void OLED_Clear(void)             { /* TODO_TI */ }

void OLED_ShowString(uint8_t x, uint8_t y, const char* str)
{
    (void)x; (void)y; (void)str;
    /* TODO_TI */
}

void OLED_DrawWaveform(int16_t* data, uint16_t len, uint8_t y_start, uint8_t height)
{
    (void)data; (void)len; (void)y_start; (void)height;
    /* TODO_TI: 把 data 归一化到 [0, height]，逐点画到屏幕 */
}

void OLED_DrawBarChart(float* values, uint8_t count, uint8_t y_start, uint8_t height)
{
    (void)values; (void)count; (void)y_start; (void)height;
    /* TODO_TI: 每个 value 画一根高度归一化的竖条 */
}
