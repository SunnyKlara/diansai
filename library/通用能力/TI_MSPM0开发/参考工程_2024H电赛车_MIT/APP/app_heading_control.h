#ifndef APP_HEADING_CONTROL_H
#define APP_HEADING_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

void app_heading_control_init(void);
void app_heading_control_start(int16_t baseSpeed, int32_t targetYawMdeg);
void app_heading_control_start_pivot(int32_t targetYawMdeg);
void app_heading_control_stop(void);
void app_heading_control_update(int32_t yawMdeg, int32_t gyroZMdps);
bool app_heading_control_active(void);
int32_t app_heading_control_error_mdeg(void);
int32_t app_heading_control_turn_x100(void);

#endif
