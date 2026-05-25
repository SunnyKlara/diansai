/**
 * @file    config.h
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include <stdint.h>

#define SYS_CLOCK_HZ        168000000UL

/* XY 龙门尺寸（mm）*/
#define XY_X_MIN_MM         0
#define XY_X_MAX_MM         400
#define XY_Y_MIN_MM         0
#define XY_Y_MAX_MM         300

/* 棋盘几何 */
#define BOARD_CELL_MM       80
#define BOX_X_MM            350
#define BOX_Y_MM            150

/* 步进 */
#define STEP_PER_REV        3200       /* 16 分频微步                  */
#define LEAD_PITCH_MM       8.0f
#define STEP_PER_MM         (STEP_PER_REV / LEAD_PITCH_MM)

#endif
