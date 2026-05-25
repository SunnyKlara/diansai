/**
 * @file    ir_sensor.h
 * @brief   5 路 TCRT5000 红外循迹传感器（数字版）
 */

#ifndef __IR_SENSOR_H
#define __IR_SENSOR_H

#include <stdint.h>
#include "../config.h"

void IR_Init(void);

/**
 * @brief 一次性读取 5 路状态
 * @param[out] raw  长度 5 数组：1=黑线，0=白色
 */
void IR_ReadAll(uint8_t raw[IR_NUM]);

#endif /* __IR_SENSOR_H */
