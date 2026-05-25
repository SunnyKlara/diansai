#include "ir_line.h"
#include "../config.h"
static volatile uint16_t s_raw[IR_NUM] = {0};
void ir_line_init(void){}
void ir_line_update_isr(const uint16_t *raw){ if (!raw) return; for (uint8_t i=0;i<IR_NUM;i++) s_raw[i]=raw[i]; }
float ir_line_get_error(void)
{
    int32_t w[IR_NUM] = {-2,-1,0,1,2};
    int32_t sw=0, sv=0;
    for (uint8_t i=0;i<IR_NUM;i++){ int32_t b = (s_raw[i] < IR_TH_LOW)?1:0; sw += w[i]*b; sv += b; }
    return (sv == 0) ? 99.0f : (float)sw / (float)sv;
}
uint8_t ir_line_at_stop_marker(void)
{
    /* 等停标志：两条平行横线 → 中间 3 路同时低（黑） */
    return (s_raw[1] < IR_TH_LOW) && (s_raw[2] < IR_TH_LOW) && (s_raw[3] < IR_TH_LOW);
}
