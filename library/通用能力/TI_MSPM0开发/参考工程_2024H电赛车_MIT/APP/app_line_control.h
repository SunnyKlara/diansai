#ifndef APP_LINE_CONTROL_H
#define APP_LINE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "BSP/bsp_gray.h"

void app_line_control_init(void);
bool app_line_control_start(int16_t baseSpeed);
bool app_line_control_start_with_bias(int16_t baseSpeed, int16_t curveBias);
bool app_line_control_start_fine(int16_t baseSpeed, int16_t curveBias);
void app_line_control_stop(void);
bool app_line_control_update(
    BspGrayLine line, int32_t leftDelta, int32_t rightDelta);
bool app_line_control_active(void);
void app_line_control_set_gains(uint16_t kpX10000, uint16_t kdX10000);
uint16_t app_line_control_kp_x10000(void);
uint16_t app_line_control_kd_x10000(void);
int16_t app_line_control_base_speed(void);
int16_t app_line_control_turn(void);
int32_t app_line_control_turn_x100(void);
int16_t app_line_control_filtered_error(void);
int16_t app_line_control_curve_bias(void);
int16_t app_line_control_yaw_error(void);
uint8_t app_line_control_lost_count(void);

#endif
