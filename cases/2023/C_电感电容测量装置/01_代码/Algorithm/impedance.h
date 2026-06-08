/**
 * @file    impedance.h
 * @brief   分压法测阻抗（DDS 激励 + ADC 双通道 + 复阻抗求解）
 */
#ifndef __IMPEDANCE_H__
#define __IMPEDANCE_H__
#include <stdint.h>

typedef struct {
    float magnitude;     /* |Z|（Ω）                  */
    float phase_deg;     /* arg(Z)（°）               */
    float resistance;    /* 实部                       */
    float reactance;     /* 虚部                       */
    float L_uH;          /* 折算电感（X > 0 时）       */
    float C_nF;          /* 折算电容（X < 0 时）       */
    float Q;             /* 品质因数                   */
} z_result_t;

void z_init(void);

/**
 * 同步对两通道（参考电阻上电压 + 待测器件上电压）做单频 DFT，得到复阻抗。
 */
void z_measure(const uint16_t *ch_ref, const uint16_t *ch_dut,
               uint32_t len, float fs_hz, float r_ref_ohm,
               z_result_t *out);
#endif
