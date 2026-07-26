#ifndef BSP_GRAY_H
#define BSP_GRAY_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_GRAY_CHANNEL_COUNT (8U)

typedef struct {
    int16_t error;
    uint16_t strength;
    uint16_t normalized[BSP_GRAY_CHANNEL_COUNT];
    bool valid;
} BspGrayLine;

void bsp_gray_init(void);
void bsp_gray_scan(void);
const uint16_t *bsp_gray_values(void);
void bsp_gray_calibration_start(void);
bool bsp_gray_calibration_stop(void);
bool bsp_gray_calibration_active(void);
bool bsp_gray_is_calibrated(void);
uint8_t bsp_gray_calibration_valid_channels(void);
const uint16_t *bsp_gray_calibration_min(void);
const uint16_t *bsp_gray_calibration_max(void);
BspGrayLine bsp_gray_line(void);

#endif
