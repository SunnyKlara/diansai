/**
 * @file    path.c
 * @brief   任务路径定义
 */

#include "path.h"
#include "../config.h"

/* 任务 (1) A→B：单条直线 */
static const Segment kTask1[] = {
    { SEG_LINE, PATH_LINE_AB_MM,   0.0f, 0 },
    { SEG_END,  0.0f,              0.0f, 0 },
};

/* 任务 (2) A→B→C→D→A：直 + 弧 + 直 + 弧 */
static const Segment kTask2[] = {
    { SEG_LINE, PATH_LINE_AB_MM,   0.0f, 0 },
    { SEG_ARC,  PATH_ARC_MM,       0.0f, 0 },   /* B→C 半圆 */
    { SEG_LINE, PATH_LINE_AB_MM,   0.0f, 0 },   /* C→D 直线（与 AB 等长）*/
    { SEG_ARC,  PATH_ARC_MM,       0.0f, 0 },   /* D→A 半圆 */
    { SEG_END,  0.0f,              0.0f, 0 },
};

/* 任务 (3) A→C→B→D→A：交叉，对角线 + 外侧弧 */
static const Segment kTask3[] = {
    { SEG_TURN, 0.0f,              PATH_TURN_AC_DEG, 0 }, /* 起步先转 38.66° 朝 C */
    { SEG_LINE, PATH_LINE_AC_MM,   0.0f, 0 },             /* A→C 对角线 */
    { SEG_TURN, 0.0f,              -(180.0f - PATH_TURN_AC_DEG), 0 },
                                                          /* 调整朝向准备进入 C→B 弧 */
    { SEG_ARC,  PATH_ARC_MM,       0.0f, 1 },             /* C→B 外侧弧（偏左）*/
    { SEG_TURN, 0.0f,              PATH_TURN_BD_DEG, 0 }, /* 调整朝向 D */
    { SEG_LINE, PATH_LINE_AC_MM,   0.0f, 0 },             /* B→D 对角线 */
    { SEG_TURN, 0.0f,              -(180.0f + PATH_TURN_BD_DEG), 0 },
    { SEG_ARC,  PATH_ARC_MM,       0.0f, 1 },             /* D→A 外侧弧 */
    { SEG_END,  0.0f,              0.0f, 0 },
};

/* 任务 (4)：把任务 (3) 重复 4 次的段序列在运行时动态构造，
 *           为了简单，这里展开 4 次（数组体积可接受）。
 *           实际比赛中可以改成"loop_count"机制更优雅。 */
static const Segment kTask4[] = {
#define ONE_LAP_3 \
    { SEG_TURN, 0.0f, PATH_TURN_AC_DEG, 0 },           \
    { SEG_LINE, PATH_LINE_AC_MM, 0.0f, 0 },             \
    { SEG_TURN, 0.0f, -(180.0f - PATH_TURN_AC_DEG), 0 },\
    { SEG_ARC,  PATH_ARC_MM, 0.0f, 1 },                 \
    { SEG_TURN, 0.0f, PATH_TURN_BD_DEG, 0 },            \
    { SEG_LINE, PATH_LINE_AC_MM, 0.0f, 0 },             \
    { SEG_TURN, 0.0f, -(180.0f + PATH_TURN_BD_DEG), 0 },\
    { SEG_ARC,  PATH_ARC_MM, 0.0f, 1 },
    ONE_LAP_3
    ONE_LAP_3
    ONE_LAP_3
    ONE_LAP_3
    { SEG_END, 0.0f, 0.0f, 0 },
#undef ONE_LAP_3
};

/* ============ 当前任务状态 ============ */
static const Segment *g_segs   = kTask2;
static uint16_t       g_index  = 0;
static uint16_t       g_total  = 0;

static uint16_t count_segments(const Segment *segs)
{
    uint16_t n = 0;
    while (segs[n].type != SEG_END) n++;
    return n;
}

void Path_Load(uint8_t task_id)
{
    switch (task_id) {
        case 1: g_segs = kTask1; break;
        case 2: g_segs = kTask2; break;
        case 3: g_segs = kTask3; break;
        case 4: g_segs = kTask4; break;
        default: g_segs = kTask2; break;
    }
    g_index = 0;
    g_total = count_segments(g_segs);
}

const Segment* Path_Current(void)
{
    return &g_segs[g_index];
}

uint8_t Path_Next(void)
{
    if (g_segs[g_index].type == SEG_END) return 0;
    g_index++;
    return (g_segs[g_index].type != SEG_END) ? 1 : 0;
}

void Path_Reset(void)
{
    g_index = 0;
}

uint16_t Path_Index(void)
{
    return g_index;
}

uint16_t Path_TotalSegments(void)
{
    return g_total;
}
