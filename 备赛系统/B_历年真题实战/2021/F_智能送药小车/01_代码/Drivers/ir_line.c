#include "ir_line.h"
#include "../config.h"

static volatile uint16_t s_raw[IR_NUM] = {0};

void ir_line_init(void) {}
void ir_line_update_isr(const uint16_t *raw)
{
    if (!raw) return;
    for (uint8_t i = 0; i < IR_NUM; i++) s_raw[i] = raw[i];
}

float ir_line_get_error(void)
{
    /* 5 路红外，加权平均：(-2,-1,0,1,2)
     * 在线（白底红线）→ 红外检测低输出值
     */
    int32_t weights[IR_NUM] = {-2, -1, 0, 1, 2};
    int32_t sum_w = 0; int32_t sum_v = 0;
    for (uint8_t i = 0; i < IR_NUM; i++) {
        uint16_t v = s_raw[i];
        int32_t b = (v < IR_THRESHOLD_LOW) ? 1 : 0;
        sum_w += weights[i] * b;
        sum_v += b;
    }
    if (sum_v == 0) return 99.0f;        /* 全部脱线 */
    return (float)sum_w / (float)sum_v;
}

uint8_t ir_line_at_t_node(void)
{
    /* T 路口：左右两侧红外都触发 */
    return (s_raw[0] < IR_THRESHOLD_LOW) && (s_raw[4] < IR_THRESHOLD_LOW) ? 1 : 0;
}
