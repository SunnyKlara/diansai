/**
 * @file    chua_oscillator.h
 * @brief   蔡氏（Chua's）混沌振荡器数值积分（4 阶 RK4）
 *          x' = α(y - x - g(x))
 *          y' = x - y + z
 *          z' = -β·y - γ·z
 *          g(x) = m1·x + 0.5·(m0-m1)·(|x+1| - |x-1|)
 */
#ifndef __CHUA_OSCILLATOR_H__
#define __CHUA_OSCILLATOR_H__
#include <stdint.h>

typedef struct {
    float x, y, z;        /* 状态                          */
    float alpha, beta, gamma;
    float m0, m1;         /* 蔡氏二极管分段斜率           */
    float dt;             /* 积分步长（s）                */
} chua_t;

void chua_init(chua_t *c, float alpha, float beta, float gamma,
               float m0, float m1, float dt);
void chua_step(chua_t *c);
void chua_reset(chua_t *c, float x0, float y0, float z0);
#endif
