/**
 * @file    twisted_pair.c
 * @brief   双绞线 4 对全检测：调用 mux_select + TDR 分析
 *
 * 与 2023 B TDR 同款，仅参数从 50Ω 同轴变为 100Ω 双绞。
 */
#include "twisted_pair.h"
#include "../config.h"

void tp_init(void) {}

void tp_test_all(tp_result_t *out)
{
    if (!out) return;
    /* 真机：循环 4 对，每对调用 mux_select + tdr_trigger + tdr_analyze */
    for (uint8_t i = 0; i < TP_NUM_PAIRS; i++) {
        out->pair[i] = TP_OK;       /* 占位 */
        out->length_m[i] = 1.0f;
    }
}
