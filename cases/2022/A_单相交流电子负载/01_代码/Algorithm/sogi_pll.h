/**
 * @file    sogi_pll.h
 * @brief   单相 SOGI-PLL（α/β 正交分量生成 + DQ 锁相）
 */
#ifndef __SOGI_PLL_H__
#define __SOGI_PLL_H__

#include <stdint.h>

typedef struct {
    float theta, omega;
    float alpha, beta;
    float v1, v2;
    float kp, ki;
    float integ;
    float ts;
} sogi_pll_t;

void sogi_pll_init(sogi_pll_t *p, float ts);
float sogi_pll_update(sogi_pll_t *p, float v_grid);

#endif
