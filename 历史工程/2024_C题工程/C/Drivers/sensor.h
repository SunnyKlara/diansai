#ifndef __SENSOR_H
#define __SENSOR_H

#include <stdint.h>

/* 灰度传感器数量 */
#define SENSOR_NUM      6

/* 传感器状态：1=检测到黑线, 0=白色地面 */
typedef struct {
    uint8_t  raw[SENSOR_NUM];    // 原始数字量 (0/1)
    uint16_t adc[SENSOR_NUM];    // ADC模拟量 (如果用模拟灰度)
    float    position;           // 加权计算的偏差值 (-3.0 ~ +3.0)
    uint8_t  line_lost;          // 丢线标志: 0=正常, 1=左丢线, 2=右丢线, 3=全丢
    uint8_t  cross_detected;     // 十字路口检测
    uint8_t  turn_detected;      // 弯道检测: 0=直道, 1=左弯, 2=右弯
} SensorData;

/* 初始化灰度传感器GPIO */
void Sensor_Init(void);

/* 读取所有传感器原始值 */
void Sensor_ReadAll(SensorData *data);

/* 计算加权偏差值 (核心算法) */
float Sensor_CalcPosition(SensorData *data);

/* 检测特殊路况 */
void Sensor_DetectPattern(SensorData *data);

/* 获取传感器数据（供外部调用） */
SensorData* Sensor_GetData(void);

#endif /* __SENSOR_H */
