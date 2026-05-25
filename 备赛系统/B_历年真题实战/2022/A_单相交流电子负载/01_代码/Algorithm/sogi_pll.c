/**
 * @file    sogi_pll.c
 */
#include "sogi_pll.h"
#include "../config.h"
#include <math.h>

void sogi_pll_init(sogi_pll_t *p, float ts)
{
    if (!p) return;
    p->theta = 0.0f;
    p->omega = PLL_FREQ_NOMINAL;
    p->alpha = p->beta = 0.0f;
    p->v1 = p->v2 = 0.0f;
    p->kp = PLL_KP;
    p->ki = PLL_KI;
    p->integ = PLL_FREQ_NOMINAL;
    p->ts = ts;
}

float sogi_pll_update(sogi_pll_t *p, float v_grid)
{
    if (!p) return 0.0f;

    /* SOGI 二阶生成 α / β（β 滞后 90°） */
    float err = v_grid - p->alpha;
    p->v1 += (SOGI_K * err - p->v2) * p->omega * p->ts;
    p->v2 += p->v1 * p->omega * p->ts;
    p->alpha = p->v1;
    p->beta  = p->v2;

    /* Park → vq */
    float c = cosf(p->theta), s = sinf(p->theta);
    float vq = -p->alpha * s + p->beta * c;

    /* PI 锁定 vq → 0 */
    p->integ += p->ki * (-vq) * p->ts;
    p->omega = p->kp * (-vq) + p->integ;
    if (p->omega > PLL_FREQ_MAX) p->omega = PLL_FREQ_MAX;
    if (p->omega < PLL_FREQ_MIN) p->omega = PLL_FREQ_MIN;

    p->theta += p->omega * p->ts;
    while (p->theta >= TWO_PI) p->theta -= TWO_PI;
    while (p->theta <  0.0f)   p->theta += TWO_PI;
    return p->theta;
}
