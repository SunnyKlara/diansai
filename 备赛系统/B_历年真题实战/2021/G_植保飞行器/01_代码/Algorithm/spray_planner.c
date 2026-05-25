/**
 * @file    spray_planner.c
 * @brief   喷洒决策：基于视觉目标 + 已喷区域记忆
 */
#include "spray_planner.h"
#include "../config.h"

void spray_init(spray_planner_t *p)
{
    if (!p) return;
    p->n_targets = 0;
    p->should_pump = 0;
}

void spray_on_target(spray_planner_t *p, const target_t *t)
{
    if (!p || !t) return;
    if (t->color != TARGET_COLOR_RED && t->color != TARGET_COLOR_GREEN) return;

    /* 已喷过的不重喷（容差 0.5m）*/
    for (uint8_t i = 0; i < p->n_targets; i++) {
        float dx = p->done[i].x - t->x;
        float dy = p->done[i].y - t->y;
        if (dx*dx + dy*dy < 0.25f) return;
    }
    if (p->n_targets >= 32) return;
    p->done[p->n_targets++] = *t;
    p->should_pump = 1;
}

uint8_t spray_should_pump(spray_planner_t *p)
{
    if (!p) return 0;
    uint8_t r = p->should_pump;
    p->should_pump = 0;
    return r;
}
