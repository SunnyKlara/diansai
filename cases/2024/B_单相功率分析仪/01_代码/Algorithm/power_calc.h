/**
 * @file    power_calc.h
 * @brief   时域功率计算（Vrms / Irms / P / S / PF）
 *
 *  原理：
 *      Vrms = √(1/N · Σ v[n]²)
 *      Irms = √(1/N · Σ i[n]²)
 *      P    = 1/N · Σ (v[n] · i[n])
 *      S    = Vrms × Irms
 *      PF   = P / S
 *
 *  与硬件解耦：输入 ADC 采样数组（已去偏置 + 缩放后的浮点物理量），
 *             输出测量结果结构体。可以脱离硬件做单元测试。
 */

#ifndef __POWER_CALC_H
#define __POWER_CALC_H

#include <stdint.h>

typedef struct {
    float v_rms;            /* 电压有效值 (V)  */
    float i_rms;            /* 电流有效值 (A)  */
    float p_active;         /* 有功功率 (W)    */
    float s_apparent;       /* 视在功率 (VA)   */
    float pf;               /* 功率因数 [-1, 1]，正=滞后/负=超前  */
} TimeDomainResult;

/**
 * @brief 时域计算
 * @param v_samples  电压采样（V，已去偏置）
 * @param i_samples  电流采样（A，已去偏置）
 * @param n          采样点数
 * @param[out] out   计算结果
 */
void PowerCalc_TimeDomain(const float *v_samples,
                          const float *i_samples,
                          uint16_t n,
                          TimeDomainResult *out);

#endif /* __POWER_CALC_H */
