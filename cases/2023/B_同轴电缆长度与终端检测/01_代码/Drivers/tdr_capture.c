/**
 * @file    tdr_capture.c
 * @brief   等效采样 TDR：高速激励 + 1MSPS ADC + 100 次延迟扫描合成 100MSPS
 */
#include "tdr_capture.h"
#include "../config.h"
#include <string.h>

static float s_wave[TDR_FRAME_LEN];
static volatile uint8_t s_ready = 0;

void tdr_init(void){ s_ready = 0; }

void tdr_trigger(void)
{
    /* 真机：触发 100 次脉冲，每次延迟 +1ns，每次采 10 点（1us 窗内） */
    s_ready = 1;
}

uint8_t tdr_collect(float *out, uint32_t len)
{
    if (!out || !s_ready || len < TDR_FRAME_LEN) return 0;
    memcpy(out, s_wave, TDR_FRAME_LEN * sizeof(float));
    s_ready = 0;
    return 1;
}
