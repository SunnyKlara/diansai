/* beep.c —— 蜂鸣器非阻塞状态机（设计判断见 beep.h 文件头） */
#include "beep.h"

static int reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

void beep_init(beep_t *B)
{
    if (!B) return;
    B->level = 0;
    B->pulses = 0;
    B->on_phase = 0;
    B->t_next = 0u;
    B->on_ms = BEEP_SHORT_MS;
    B->gap_ms = BEEP_GAP_MS;
}

void beep_req(beep_t *B, beep_pat_t pat, uint32_t now_ms)
{
    if (!B) return;
    switch (pat) {
    case BEEP_SHORT:  B->pulses = 1; B->on_ms = BEEP_SHORT_MS; B->gap_ms = BEEP_GAP_MS; break;
    case BEEP_DOUBLE: B->pulses = 2; B->on_ms = BEEP_SHORT_MS; B->gap_ms = BEEP_GAP_MS; break;
    case BEEP_LONG:   B->pulses = 1; B->on_ms = BEEP_LONG_MS;  B->gap_ms = BEEP_GAP_MS; break;
    default:
        B->pulses = 0; B->on_phase = 0; B->level = 0;   /* 立即静音 */
        return;
    }
    B->on_phase = 1;
    B->level    = 1;
    B->t_next   = now_ms + B->on_ms;
}

int beep_step(beep_t *B, uint32_t now_ms)
{
    if (!B) return 0;
    if (B->pulses == 0) { B->level = 0; return 0; }      /* 空闲：恒静音（设计判断 2） */

    if (reached(now_ms, B->t_next)) {
        if (B->on_phase) {
            B->on_phase = 0;
            B->pulses--;
            if (B->pulses == 0) { B->level = 0; return 0; }   /* 图案放完 -> 关 */
            B->level  = 0;
            B->t_next = now_ms + B->gap_ms;
        } else {
            B->on_phase = 1;
            B->level    = 1;
            B->t_next   = now_ms + B->on_ms;
        }
    }
    return B->level ? 1 : 0;
}

int beep_busy(const beep_t *B)
{
    return (B && B->pulses > 0) ? 1 : 0;
}
