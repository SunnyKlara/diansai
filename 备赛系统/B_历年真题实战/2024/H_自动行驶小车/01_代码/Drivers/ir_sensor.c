/**
 * @file    ir_sensor.c
 * @brief   MSPM0G3507 + 5 路 TCRT5000 红外（ADC0 模式）
 *
 * 资源映射：
 *   IR1 PA24 / IR2 PA25 / IR3 PA26 / IR4 PA27 / IR5 PA15
 *   均接 ADC0 多通道顺序扫描（IN5/IN6/IN7/IN10/IN9）
 *
 * 输出策略：
 *   ADC 12bit (0~4095)，黑线输出 < 1500，白色 > 2500，
 *   可调阈值（config.h 中 IR_THRESHOLD_LOW / HIGH）做迟滞二值化。
 */

#include "ir_sensor.h"
#include "../config.h"
#include <ti/driverlib/driverlib.h>
#include "ti_msp_dl_config.h"

static uint8_t s_last_bin[IR_NUM] = {0, 0, 0, 0, 0};

static uint16_t read_one(uint8_t mem_idx)
{
    /* 单次软件触发，等转换完成读 MEMRES */
    DL_ADC12_startConversion(ADC0_INST);
    while (false ==
           DL_ADC12_getRawInterruptStatus(ADC0_INST,
                                          DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED << mem_idx)) {
        /* 单通道转换 < 200ns，busy-wait 即可 */
    }
    DL_ADC12_clearInterruptStatus(ADC0_INST,
                                  DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED << mem_idx);
    return (uint16_t)DL_ADC12_getMemResult(ADC0_INST,
                                           DL_ADC12_MEM_IDX_0 + mem_idx);
}

void IR_Init(void)
{
    /* SysConfig 已配置 ADC0 + 5 个 MEM 通道顺序扫描。
     * 这里只清状态。
     */
    DL_ADC12_enableConversions(ADC0_INST);
    for (uint8_t i = 0; i < IR_NUM; i++) s_last_bin[i] = 0;
}

void IR_ReadAll(uint8_t raw[IR_NUM])
{
    for (uint8_t i = 0; i < IR_NUM; i++) {
        uint16_t val = read_one(i);

        /* 迟滞二值化：上次=黑保持黑直到值过 HIGH；上次=白保持白直到值低于 LOW */
        if (s_last_bin[i] == 1) {
            /* 当前是黑 */
            if (val > IR_THRESHOLD_HIGH) s_last_bin[i] = 0;
        } else {
            /* 当前是白 */
            if (val < IR_THRESHOLD_LOW) s_last_bin[i] = 1;
        }
        raw[i] = s_last_bin[i];
    }
}
