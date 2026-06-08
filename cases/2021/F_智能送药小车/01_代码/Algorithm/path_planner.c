#include "path_planner.h"
#include "../config.h"

/* 病房布局：1-2 在 T 节点 1，3-4 在 T 节点 2，5-6 在 T 节点 3，7-8 在 T 节点 4
 * 路径动作 = 直走到对应 T 节点 → 左/右转入病房 → 停 */

void path_init(path_planner_t *p)
{
    if (!p) return;
    p->target_room = 0;
    p->step = 0;
    p->done = 0;
}

void path_set_target(path_planner_t *p, uint8_t room)
{
    if (!p) return;
    if (room < 1 || room > ROOM_MAX) room = 1;
    p->target_room = room;
    p->step = 0;
    p->done = 0;
}

path_action_t path_next_action(path_planner_t *p, uint8_t at_t_node, uint8_t at_room)
{
    if (!p || p->done) return ACT_STOP;
    /* 走廊节点：1=T1, 2=T2, 3=T3, 4=T4 */
    uint8_t target_t = (uint8_t)((p->target_room - 1) / 2 + 1);
    uint8_t side     = ((p->target_room - 1) % 2) ? 1 : 0;  /* 0=左, 1=右 */

    if (at_room) {
        p->done = 1;
        return ACT_STOP;
    }

    if (at_t_node == target_t) {
        /* 转向进入病房 */
        return side ? ACT_TURN_RIGHT : ACT_TURN_LEFT;
    }

    return ACT_FORWARD;
}
