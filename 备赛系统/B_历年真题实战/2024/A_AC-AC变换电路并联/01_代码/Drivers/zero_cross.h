/**
 * @file    zero_cross.h
 * @brief   电网过零检测（PC817 光耦 + GPIO EXTI）
 */
#ifndef __ZERO_CROSS_H__
#define __ZERO_CROSS_H__
#include <stdint.h>

typedef enum { ZCD_NEG_TO_POS = 0, ZCD_POS_TO_NEG = 1 } zcd_edge_t;

void zcd_init(void);
uint8_t zcd_get_polarity(void);            /* 1 = 正半周, 0 = 负半周 */
zcd_edge_t zcd_get_last_edge(void);
uint32_t zcd_get_period_us(void);          /* 测得半周期 us（应 ≈ 10000） */

#endif
