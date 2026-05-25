#include "oled.h"
#include <stdio.h>
void oled_init(void) {}
void oled_clear(void) {}
void oled_show(float vout, float iout, float vbus, float pf, float eta)
{
    char line[40];
    snprintf(line, sizeof(line), "Uo=%.2fV Io=%.2fA",
             (double)vout, (double)iout);
    snprintf(line, sizeof(line), "Vb=%.0fV PF=%.3f", (double)vbus, (double)pf);
    snprintf(line, sizeof(line), "eta=%.2f", (double)eta);
    (void)line;
}
