#ifndef BSP_MOTOR_H
#define BSP_MOTOR_H

#include <stdint.h>

void bsp_motor_init(void);
void bsp_motor_set(int16_t leftPermille, int16_t rightPermille);
void bsp_motor_stop(void);
int16_t bsp_motor_left_command(void);
int16_t bsp_motor_right_command(void);

#endif
