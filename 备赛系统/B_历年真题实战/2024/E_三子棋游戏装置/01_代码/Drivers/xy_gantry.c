#include "xy_gantry.h"
#include "../config.h"

static int32_t s_x = 0, s_y = 0;
void xy_init(void){ s_x = 0; s_y = 0; }
void xy_home(void){ s_x = 0; s_y = 0; }
void xy_move_to(int32_t x_mm, int32_t y_mm)
{
    if (x_mm < XY_X_MIN_MM) x_mm = XY_X_MIN_MM;
    if (x_mm > XY_X_MAX_MM) x_mm = XY_X_MAX_MM;
    if (y_mm < XY_Y_MIN_MM) y_mm = XY_Y_MIN_MM;
    if (y_mm > XY_Y_MAX_MM) y_mm = XY_Y_MAX_MM;
    s_x = x_mm; s_y = y_mm;
    /* 真机：DRV8825 STEP 信号脉冲 */
}
void xy_get_pos(int32_t *x, int32_t *y){ if(x) *x=s_x; if(y) *y=s_y; }
void xy_pickup(uint8_t enable){ (void)enable; /* 电磁铁 GPIO 控制 */ }
