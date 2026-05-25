/**
 * @file    park_clarke.c
 */
#include "park_clarke.h"
#include "../config.h"
#include <math.h>

void clarke_transform(float a, float b, float c, float *alpha, float *beta)
{
    if (alpha) *alpha = a - 0.5f * (b + c);
    if (beta)  *beta  = (b - c) * (SQRT3 * 0.5f);
    /* 等幅变换：上面是非平衡的，再乘 2/3 化为标准 */
    if (alpha) *alpha *= (2.0f / 3.0f);
    if (beta)  *beta  *= (2.0f / 3.0f);
}

void park_transform(float alpha, float beta, float theta, float *d, float *q)
{
    float c = cosf(theta), s = sinf(theta);
    if (d) *d =  alpha * c + beta * s;
    if (q) *q = -alpha * s + beta * c;
}

void inv_park_transform(float d, float q, float theta, float *alpha, float *beta)
{
    float c = cosf(theta), s = sinf(theta);
    if (alpha) *alpha = d * c - q * s;
    if (beta)  *beta  = d * s + q * c;
}

void inv_clarke_transform(float alpha, float beta, float *a, float *b, float *c)
{
    if (a) *a = alpha;
    if (b) *b = -0.5f * alpha + (SQRT3 * 0.5f) * beta;
    if (c) *c = -0.5f * alpha - (SQRT3 * 0.5f) * beta;
}

void pll_init(pll_t *p, float ts)
{
    if (!p) return;
    p->theta = 0.0f;
    p->omega = TWO_PI * GRID_FREQ_HZ;
    p->kp    = 100.0f;
    p->ki    = 1000.0f;
    p->integ = 0.0f;
    p->ts    = ts;
}

float pll_update(pll_t *p, float vq_err)
{
    if (!p) return 0.0f;
    /* SRF-PLL：vq → 0 时锁定 */
    p->integ += p->ki * vq_err * p->ts;
    p->omega = TWO_PI * GRID_FREQ_HZ + p->kp * vq_err + p->integ;
    p->theta += p->omega * p->ts;
    while (p->theta >= TWO_PI) p->theta -= TWO_PI;
    while (p->theta <  0.0f)   p->theta += TWO_PI;
    return p->theta;
}
