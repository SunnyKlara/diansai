#ifndef __HALL_SENSOR_H__
#define __HALL_SENSOR_H__
#include <stdint.h>
void hall_init(void);
void hall_on_adc(const uint16_t *raw);
float hall_v2mm(float v);
float hall_get_mm(uint8_t idx);   /* 0=center, 1=front, 2=back, 3=left, 4=right */
#endif
