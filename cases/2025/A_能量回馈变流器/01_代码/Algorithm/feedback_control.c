/**
 * @file    feedback_control.c
 * @brief   能量回馈（同步整流）控制实现
 *
 *  这是 2025A 发挥部分 30 分的核心算法。
 *  原则：宁可保守一点（多让二极管导通），也不要冒险（同时导通造成直通）。
 */

#include "feedback_control.h"
#include "../config.h"

#include <math.h>

void FB_Init(FeedbackCtrl *fb)
{
    if (!fb) return;
    for (uint8_t p = 0; p < 3u; p++) {
        fb->state[p] = SR_OFF;
        fb->i_phase[p] = 0.0f;
    }
    fb->enable_count = 0u;
    fb->enabled = 0u;
}

void FB_Enable(FeedbackCtrl *fb, uint8_t enabled)
{
    if (!fb) return;
    if (!enabled) {
        /* 失能 → 全部关断 */
        for (uint8_t p = 0; p < 3u; p++) fb->state[p] = SR_OFF;
    }
    fb->enabled = enabled;
}

/**
 * 单相 SR 状态决策
 *
 *  使用滞回死区（hysteresis）避免电流过零附近抖动：
 *    电流 >  +SR_THRESHOLD_A   → SR_HIGH_ON
 *    电流 <  -SR_THRESHOLD_A   → SR_LOW_ON
 *    -SR_THRESHOLD_A ≤ I ≤ +SR_THRESHOLD_A
 *      → 保持上次状态？不行——保持可能导致反向电流
 *      → 直接 SR_OFF（让 body diode 自然导通，损失 0.7V × I_small ≈ 微小）
 *
 *  这是国一队伍调试中的经验：宁可在过零点附近损失一点效率，
 *  也不能冒"同步整流持续导通时电流反向"的风险。
 */
static SRPhaseState decide_phase_state(float i_phase)
{
    if (i_phase > (float)SR_THRESHOLD_A)  return SR_HIGH_ON;
    if (i_phase < -(float)SR_THRESHOLD_A) return SR_LOW_ON;
    return SR_OFF;
}

void FB_Update(FeedbackCtrl *fb, float ia, float ib, float ic)
{
    if (!fb) return;
    fb->i_phase[0] = ia;
    fb->i_phase[1] = ib;
    fb->i_phase[2] = ic;

    if (!fb->enabled) {
        /* 未启用：保持全 OFF（让硬件二极管承担整流）*/
        for (uint8_t p = 0; p < 3u; p++) fb->state[p] = SR_OFF;
        return;
    }

    fb->state[0] = decide_phase_state(ia);
    fb->state[1] = decide_phase_state(ib);
    fb->state[2] = decide_phase_state(ic);
    fb->enable_count++;
}

SRPhaseState FB_GetState(const FeedbackCtrl *fb, uint8_t phase)
{
    if (!fb || phase >= 3u) return SR_OFF;
    return fb->state[phase];
}

void FB_Disable(FeedbackCtrl *fb)
{
    if (!fb) return;
    for (uint8_t p = 0; p < 3u; p++) fb->state[p] = SR_OFF;
    fb->enabled = 0u;
}
