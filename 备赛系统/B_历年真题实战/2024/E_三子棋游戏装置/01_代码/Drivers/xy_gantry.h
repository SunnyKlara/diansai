#ifndef __XY_GANTRY_H__
#define __XY_GANTRY_H__
#include <stdint.h>
void xy_init(void);
void xy_home(void);
void xy_move_to(int32_t x_mm, int32_t y_mm);
void xy_get_pos(int32_t *x, int32_t *y);
void xy_pickup(uint8_t enable);
#endif
