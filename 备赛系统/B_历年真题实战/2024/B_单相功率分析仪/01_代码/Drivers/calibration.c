/**
 * @file    calibration.c
 * @brief   校准管理实现
 *
 *  TODO_TI：
 *      在 MSP430 上把下面 s_persistent 改用 #pragma PERSISTENT 放到 FRAM。
 *      在标准 PC / 仿真环境保留 RAM 即可（用于离线测试）。
 */

#include "calibration.h"
#include "adc_dual.h"
#include <string.h>

/* ============================================================
 *  FRAM 区（运行期可写、断电不丢失）
 * ============================================================ */
#if defined(__MSP430__)
    #pragma PERSISTENT(s_persistent)
#endif
static CalibrationData s_persistent = {
    .magic    = 0u,             /* 上电时 0 → Load 兜底用初值 */
    .v_gain   = 0.0f,
    .v_offset = 0.0f,
    .i_gain   = 0.0f,
    .i_offset = 0.0f,
    .crc      = 0u,
};

/* 当前运行时校准（每次 Load / Save 都同步刷新到这里）*/
static CalibrationData s_runtime;

/* ============================================================
 *  本地工具
 * ============================================================ */
static uint32_t calc_crc(const CalibrationData *d)
{
    /* 简单异或校验：把所有字段（除 crc 自身）按 32bit 字累异或 */
    const uint32_t *p = (const uint32_t *)d;
    uint32_t c = 0u;
    /* CalibrationData 共 6 个 32bit 字；最后一个是 crc 不参与 */
    for (uint8_t i = 0; i < 5u; i++) c ^= p[i];
    return c ^ 0xA5A5A5A5u;
}

static void apply_to_adc(const CalibrationData *d)
{
    extern void ADCDual_SetCalibration(float, float, float, float);
    ADCDual_SetCalibration(d->v_gain, d->v_offset, d->i_gain, d->i_offset);
}

static void load_factory_defaults(CalibrationData *d)
{
    d->magic    = CALIB_MAGIC;
    d->v_gain   = V_GAIN_INIT;
    d->v_offset = V_OFFSET_INIT;
    d->i_gain   = I_GAIN_INIT;
    d->i_offset = I_OFFSET_INIT;
    d->crc      = calc_crc(d);
}

/* ============================================================
 *  公开接口
 * ============================================================ */
void Calib_Init(void)
{
    /* 把 FRAM 中的数据复制到运行时副本 */
    memcpy(&s_runtime, &s_persistent, sizeof(s_runtime));
}

uint8_t Calib_Load(void)
{
    Calib_Init();

    uint8_t valid = (s_runtime.magic == CALIB_MAGIC) &&
                    (s_runtime.crc   == calc_crc(&s_runtime));
    if (!valid) {
        load_factory_defaults(&s_runtime);
    }
    apply_to_adc(&s_runtime);
    return valid ? 1u : 0u;
}

uint8_t Calib_Save(const CalibrationData *src)
{
    if (!src) return 0u;

    CalibrationData tmp = *src;
    tmp.magic = CALIB_MAGIC;
    tmp.crc   = calc_crc(&tmp);

    /* 写 FRAM 持久层 + 运行时副本 */
    memcpy(&s_persistent, &tmp, sizeof(tmp));
    memcpy(&s_runtime,    &tmp, sizeof(tmp));

    apply_to_adc(&s_runtime);
    return 1u;
}

void Calib_GetCurrent(CalibrationData *out)
{
    if (out) memcpy(out, &s_runtime, sizeof(*out));
}

void Calib_Reset(void)
{
    CalibrationData d;
    load_factory_defaults(&d);
    Calib_Save(&d);
}

void Calib_ComputeAndApply(float v_rms_meas, float v_rms_real,
                           float i_rms_meas, float i_rms_real)
{
    CalibrationData d = s_runtime;

    /* 用现有 gain 反推新 gain：
     *      v_real / v_meas = (gain_new) / (gain_old)
     *  → gain_new = gain_old × (real / meas)
     */
    if (v_rms_meas > 0.5f && v_rms_real > 0.5f) {
        d.v_gain = d.v_gain * (v_rms_real / v_rms_meas);
    }
    if (i_rms_meas > 0.005f && i_rms_real > 0.005f) {
        d.i_gain = d.i_gain * (i_rms_real / i_rms_meas);
    }

    Calib_Save(&d);
}
