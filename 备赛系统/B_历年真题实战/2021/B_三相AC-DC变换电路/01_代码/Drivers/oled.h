#ifndef __OLED_H__
#define __OLED_H__
#include <stdint.h>
void oled_init(void);
void oled_clear(void);
void oled_show(float vout, float iout, float vbus, float pf, float eta);
#endif
