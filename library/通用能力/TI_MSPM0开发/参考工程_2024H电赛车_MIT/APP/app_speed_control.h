#ifndef APP_SPEED_CONTROL_H
#define APP_SPEED_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

void app_speed_control_init(void);
void app_speed_control_start(int16_t leftTarget, int16_t rightTarget);
void app_speed_control_set_targets(int16_t leftTarget, int16_t rightTarget);
void app_speed_control_set_targets_x100(
    int32_t leftTargetX100, int32_t rightTargetX100);
void app_speed_control_set_slew_step_x100(uint16_t stepX100);
void app_speed_control_stop(void);
void app_speed_control_update(int32_t leftDelta, int32_t rightDelta);
bool app_speed_control_active(void);
void app_speed_control_set_gains(
    uint16_t kpX100, uint16_t kiX100, uint16_t feedforwardX100);
uint16_t app_speed_control_kp_x100(void);
uint16_t app_speed_control_ki_x100(void);
uint16_t app_speed_control_feedforward_x100(void);
int16_t app_speed_control_left_request(void);
int16_t app_speed_control_right_request(void);
int16_t app_speed_control_left_target(void);
int16_t app_speed_control_right_target(void);

#endif
