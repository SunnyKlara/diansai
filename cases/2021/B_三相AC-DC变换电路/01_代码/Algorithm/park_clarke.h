/**
 * @file    park_clarke.h
 * @brief   Clarke / Park 变换 + PLL（与 2025 A 同款思路）
 */
#ifndef __PARK_CLARKE_H__
#define __PARK_CLARKE_H__

#include <stdint.h>

/* abc → αβ */
void clarke_transform(float a, float b, float c, float *alpha, float *beta);
/* αβ → dq */
void park_transform(float alpha, float beta, float theta, float *d, float *q);
/* dq → αβ */
void inv_park_transform(float d, float q, float theta, float *alpha, float *beta);
/* αβ → abc（SVPWM 用） */
void inv_clarke_transform(float alpha, float beta, float *a, float *b, float *c);

/* 简化 SOGI-PLL：基于 αβ 锁相得到 θ 与 ω */
typedef struct {
    float theta;
    float omega;
    float kp;
    float ki;
    float integ;
    float ts;
} pll_t;

void pll_init(pll_t *p, float ts);
float pll_update(pll_t *p, float vd_err);

#endif
