#ifndef __PATH_PLANNER_H__
#define __PATH_PLANNER_H__
#include <stdint.h>

typedef enum {
    ACT_FORWARD = 0,
    ACT_TURN_LEFT,
    ACT_TURN_RIGHT,
    ACT_STOP,
    ACT_BACK
} path_action_t;

typedef struct {
    uint8_t target_room;
    uint8_t step;
    uint8_t done;
} path_planner_t;

void path_init(path_planner_t *p);
void path_set_target(path_planner_t *p, uint8_t room);
path_action_t path_next_action(path_planner_t *p, uint8_t at_t_node, uint8_t at_room);

#endif
