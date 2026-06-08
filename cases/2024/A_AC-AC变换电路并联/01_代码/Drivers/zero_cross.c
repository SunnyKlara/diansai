/**
 * @file    zero_cross.c
 */
#include "zero_cross.h"

static volatile uint8_t    s_polarity = 1;
static volatile zcd_edge_t s_last_edge = ZCD_NEG_TO_POS;
static volatile uint32_t   s_period_us = 10000;

void zcd_init(void)
{
    s_polarity = 1;
    s_last_edge = ZCD_NEG_TO_POS;
    s_period_us = 10000;
}

uint8_t zcd_get_polarity(void)        { return s_polarity; }
zcd_edge_t zcd_get_last_edge(void)    { return s_last_edge; }
uint32_t zcd_get_period_us(void)      { return s_period_us; }

/* EXTI ISR 回调 —— 真机里 HAL_GPIO_EXTI_Callback() 调用 */
void zcd_on_edge_isr(uint32_t timestamp_us)
{
    static uint32_t last_ts = 0;
    if (last_ts != 0) {
        s_period_us = timestamp_us - last_ts;
    }
    last_ts = timestamp_us;
    s_polarity = s_polarity ? 0 : 1;
    s_last_edge = s_polarity ? ZCD_NEG_TO_POS : ZCD_POS_TO_NEG;
}
