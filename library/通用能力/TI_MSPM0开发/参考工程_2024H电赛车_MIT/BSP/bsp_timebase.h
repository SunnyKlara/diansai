#ifndef BSP_TIMEBASE_H
#define BSP_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

void bsp_timebase_init(void);
bool bsp_timebase_take_tick(void);
uint32_t bsp_timebase_now_ms(void);

#endif
