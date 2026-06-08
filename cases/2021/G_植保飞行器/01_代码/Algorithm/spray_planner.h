#ifndef __SPRAY_PLANNER_H__
#define __SPRAY_PLANNER_H__
#include <stdint.h>

typedef struct {
    float x, y;       /* 视觉目标坐标（m）*/
    uint8_t color;    /* 1=绿, 2=红 */
} target_t;

typedef struct {
    target_t done[32];     /* 已喷过的位置 */
    uint8_t n_targets;
    uint8_t should_pump;
} spray_planner_t;

void spray_init(spray_planner_t *p);
void spray_on_target(spray_planner_t *p, const target_t *t);
uint8_t spray_should_pump(spray_planner_t *p);
#endif
