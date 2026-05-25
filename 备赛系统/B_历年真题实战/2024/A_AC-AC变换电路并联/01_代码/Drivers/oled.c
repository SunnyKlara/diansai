/**
 * @file    oled.c
 */
#include "oled.h"
#include <stdio.h>

static void _hal_send(const uint8_t *buf, uint16_t len) { (void)buf; (void)len; }

void oled_init(void)  { uint8_t cmd = 0xAF; _hal_send(&cmd, 1); }
void oled_clear(void) { /* 全屏 0 */ }

void oled_show_status(float vset, float vrms, float iout, float duty)
{
    char line[32];
    snprintf(line, sizeof(line), "Vset=%.1f", (double)vset);
    /* TODO: drawString(0, 0, line) */
    snprintf(line, sizeof(line), "Vrms=%.2f", (double)vrms);
    snprintf(line, sizeof(line), "Iout=%.2f", (double)iout);
    snprintf(line, sizeof(line), "Duty=%.2f", (double)duty);
    (void)line;
}
