/**
 * @file    oled.h
 * @brief   0.96" SSD1306 OLED I2C 占位
 */
#ifndef __OLED_H__
#define __OLED_H__
#include <stdint.h>

void oled_init(void);
void oled_clear(void);
void oled_show_status(float vset, float vrms, float iout, float duty);

#endif
