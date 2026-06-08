/**
 * @file    track.c
 * @brief   弧线段循迹实现
 */

#include "track.h"
#include "pid.h"
#include "../config.h"

#define LOST_POSITION  (-10.0f)

static const int8_t kWeights[IR_NUM] = {
    IR_WEIGHT_0, IR_WEIGHT_1, IR_WEIGHT_2, IR_WEIGHT_3, IR_WEIGHT_4,
};

static PID_Controller s_track_pid;
static TrackState     s_state          = TRACK_DONE;
static float          s_expect_arc_mm  = 0.0f;
static uint8_t        s_dir_hint       = 0;
static float          s_last_position  = 0.0f;
static uint16_t       s_all_white_cnt  = 0;
static uint16_t       s_lost_ms_cnt    = 0;

float Track_CalcPosition(const uint8_t ir_raw[5])
{
    int sum    = 0;
    int active = 0;
    for (int i = 0; i < IR_NUM; i++) {
        if (ir_raw[i]) {
            sum    += kWeights[i];
            active += 1;
        }
    }
    if (active == 0) return LOST_POSITION;
    return (float)sum / (float)active;
}

void Track_Begin(float expect_arc_mm, uint8_t dir_hint)
{
    PID_Init(&s_track_pid, PID_TRACK_KP, PID_TRACK_KI, PID_TRACK_KD,
             PID_TRACK_OUT_MIN, PID_TRACK_OUT_MAX);
    PID_SetTarget(&s_track_pid, 0.0f);

    s_state         = TRACK_RUNNING;
    s_expect_arc_mm = expect_arc_mm;
    s_dir_hint      = dir_hint;
    s_last_position = 0.0f;
    s_all_white_cnt = 0;
    s_lost_ms_cnt   = 0;
}

TrackState Track_Update(const uint8_t ir_raw[5], float trip_mm,
                        float *tgt_v_left, float *tgt_v_right)
{
    if (s_state != TRACK_RUNNING) {
        if (tgt_v_left)  *tgt_v_left  = 0.0f;
        if (tgt_v_right) *tgt_v_right = 0.0f;
        return s_state;
    }

    /* ---- 计算位置 ---- */
    float pos = Track_CalcPosition(ir_raw);
    uint8_t lost = (pos == LOST_POSITION) ? 1u : 0u;

    if (lost) {
        /* 用上次方向继续修正 */
        pos = (s_last_position >= 0.0f) ? 2.0f : -2.0f;
        s_lost_ms_cnt += CTRL_TRACK_MS;
        if (s_lost_ms_cnt > IR_LOST_TIMEOUT_MS) {
            s_state = TRACK_LOST;
            if (tgt_v_left)  *tgt_v_left  = 0.0f;
            if (tgt_v_right) *tgt_v_right = 0.0f;
            return s_state;
        }
    } else {
        s_lost_ms_cnt   = 0;
        s_last_position = pos;
    }

    /* ---- 弧线段退出判定：双条件 AND ---- */
    {
        int active = 0;
        for (int i = 0; i < IR_NUM; i++) if (ir_raw[i]) active++;
        if (active == 0) {
            s_all_white_cnt++;
        } else {
            s_all_white_cnt = 0;
        }
    }

    uint8_t arc_distance_ok = (trip_mm >= s_expect_arc_mm * 0.9f) ? 1u : 0u;
    uint8_t all_white_ok    = (s_all_white_cnt >= IR_ARC_END_TICKS) ? 1u : 0u;

    if (arc_distance_ok && all_white_ok) {
        s_state = TRACK_DONE;
        if (tgt_v_left)  *tgt_v_left  = 0.0f;
        if (tgt_v_right) *tgt_v_right = 0.0f;
        return s_state;
    }

    /* ---- 偏差 PID ---- */
    /* 偏左（pos<0）→ 输出为负 → 左轮+，右轮- → 右转回线 */
    float diff = PID_Compute(&s_track_pid, pos);
    /* 反方向：负 diff = 右修正，左轮加速 */
    float left  = (float)V_ARC_PWM - diff;
    float right = (float)V_ARC_PWM + diff;

    /* dir_hint == 1 表示偏左侧弧线（外侧弧），加 bias */
    if (s_dir_hint == 1) {
        left  -= 30.0f;
        right += 30.0f;
    } else if (s_dir_hint == 2) {
        left  += 30.0f;
        right -= 30.0f;
    }

    if (tgt_v_left)  *tgt_v_left  = left;
    if (tgt_v_right) *tgt_v_right = right;

    return s_state;
}

uint8_t Track_IsDone(void)
{
    return (s_state == TRACK_DONE) ? 1u : 0u;
}

void Track_Reset(void)
{
    PID_Reset(&s_track_pid);
    s_state          = TRACK_DONE;
    s_expect_arc_mm  = 0.0f;
    s_last_position  = 0.0f;
    s_all_white_cnt  = 0;
    s_lost_ms_cnt    = 0;
}
