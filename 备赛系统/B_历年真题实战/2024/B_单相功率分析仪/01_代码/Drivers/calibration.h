/**
 * @file    calibration.h
 * @brief   电压 / 电流校准系数管理（FRAM 持久化）
 *
 *  为什么要校准：
 *      ZMPT101B 标称 1:1，实测变比 ±2%；ZMCT103C 穿心 ±0.5%。
 *      这些误差远大于 ADC 量化（0.024%），是"达标 1%"的最大障碍。
 *      校准后通过 v_gain / i_gain 修正，可将系统精度推到 0.5% 以内。
 *
 *  数据流：
 *      上电         → Calib_Init() → Calib_Load() → 写入 ADC 模块
 *      校准命令     → Calib_ComputeAndApply(meas, real) → 反推 gain → 写 FRAM
 *      恢复出厂     → Calib_Reset() → 写 V_GAIN_INIT 等初值
 *
 *  FRAM 优势（vs 普通 Flash）：
 *      - 写入功耗低 100×（适合频繁更新的校准 / 日志）
 *      - 无擦除时间，普通赋值即持久化
 *      - 在 MSP430 中通过 #pragma PERSISTENT 直接声明
 */

#ifndef __CALIBRATION_H
#define __CALIBRATION_H

#include <stdint.h>
#include "../config.h"

typedef struct {
    uint32_t magic;       /* 等于 CALIB_MAGIC 才视为有效 */
    float    v_gain;
    float    v_offset;
    float    i_gain;
    float    i_offset;
    uint32_t crc;         /* 简单异或校验 */
} CalibrationData;

void    Calib_Init(void);

/** @brief 加载校准数据并写入 ADC 模块
 *  @return 1: 加载有效校准，0: magic/crc 不对 → 已用初值兜底
 */
uint8_t Calib_Load(void);

/** @brief 写入校准数据到 FRAM 并同步到 ADC 模块
 *  @return 1: 成功
 */
uint8_t Calib_Save(const CalibrationData *src);

/** @brief 取出当前校准数据（供 UART 输出 / 调试）*/
void    Calib_GetCurrent(CalibrationData *out);

/** @brief 恢复出厂初值（来自 config.h 的 V_GAIN_INIT 等）*/
void    Calib_Reset(void);

/**
 * @brief 用"实测有效值"和"真实有效值"反推校准系数
 *
 *  原理：当前实测 v_rms_meas 已经使用了旧的 v_gain；
 *        新 v_gain = v_gain × (real / meas)。
 *
 *  注意：本函数只调整 gain，不调整 offset。  
 *        offset 校准要专门做 0V/0A 输入的零点测量。
 *
 * @param v_rms_meas  当前装置读出的 Vrms（已经用旧 gain 算过的）
 * @param v_rms_real  标准 PA / 可调电源给出的 Vrms 真实值
 * @param i_rms_meas  当前装置读出的 Irms
 * @param i_rms_real  标准电流真实值
 */
void    Calib_ComputeAndApply(float v_rms_meas, float v_rms_real,
                              float i_rms_meas, float i_rms_real);

#endif /* __CALIBRATION_H */
