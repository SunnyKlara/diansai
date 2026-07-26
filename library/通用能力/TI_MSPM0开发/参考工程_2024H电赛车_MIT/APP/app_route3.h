#ifndef APP_ROUTE3_H
#define APP_ROUTE3_H

#include <stdbool.h>
#include <stdint.h>

#include "BSP/bsp_encoder.h"
#include "BSP/bsp_gray.h"

typedef enum {
    APP_ROUTE3_IDLE = 0,
    APP_ROUTE3_AC_STRAIGHT,
    APP_ROUTE3_C_REACHED,
    APP_ROUTE3_C_TURN_ACQUIRE,
    APP_ROUTE3_C_LINE_ACQUIRE,
    APP_ROUTE3_CB_ARC,
    APP_ROUTE3_B_REACHED,
    APP_ROUTE3_B_TURN,
    APP_ROUTE3_BD_STRAIGHT,
    APP_ROUTE3_D_TURN_ACQUIRE,
    APP_ROUTE3_D_LINE_ACQUIRE,
    APP_ROUTE3_DA_ARC,
    APP_ROUTE3_A_REACHED,
    APP_ROUTE3_FAILED,
} AppRoute3State;

typedef enum {
    APP_ROUTE3_EVENT_NONE = 0,
    APP_ROUTE3_EVENT_C_REACHED,
    APP_ROUTE3_EVENT_C_PASSED,
    APP_ROUTE3_EVENT_DISTANCE_GUARD,
    APP_ROUTE3_EVENT_B_REACHED,
    APP_ROUTE3_EVENT_B_PASSED,
    APP_ROUTE3_EVENT_D_PASSED,
    APP_ROUTE3_EVENT_A_REACHED,
    APP_ROUTE3_EVENT_ACQUIRE_GUARD,
    APP_ROUTE3_EVENT_BD_DISTANCE_GUARD,
    APP_ROUTE3_EVENT_D_ACQUIRE_GUARD,
    APP_ROUTE3_EVENT_TURN_GUARD,
} AppRoute3Event;

void app_route3_init(void);
bool app_route3_start_ac(void);
bool app_route3_start_acb(void);
bool app_route3_start_full(void);
bool app_route3_start_cb(void);
void app_route3_stop(void);
AppRoute3Event app_route3_update_10ms(
    BspGrayLine line, BspEncoderSample encoder);
AppRoute3State app_route3_state(void);
int32_t app_route3_distance_counts(void);
bool app_route3_endpoint_armed(void);
uint8_t app_route3_endpoint_lost_count(void);

#endif
