/**
 * @file    encoder.c
 * @brief   MSPM0G3507 + JGA25-370 编码器（TIMG0/TIMG6 QEI 模式）
 *
 * 资源映射（与 引脚分配表.md 同步）：
 *   左轮  PA12 (CCP0) / PA13 (CCP1) → TIMG0 QEI Mode
 *   右轮  PA21 (CCP0) / PA22 (CCP1) → TIMG6 QEI Mode
 *   16 bit 计数器，4× 倍频（每个 AB 边沿都计数）
 */

#include "encoder.h"
#include "../config.h"
#include <ti/driverlib/driverlib.h>
#include "ti_msp_dl_config.h"

/* SysConfig 生成的实例宏（按你的 .syscfg 命名调整）：
 *   ENC_LEFT_INST   = TIMG0
 *   ENC_RIGHT_INST  = TIMG6
 */

static int32_t s_last_left_count  = 0;
static int32_t s_last_right_count = 0;
static float   s_trip_left_mm     = 0.0f;
static float   s_trip_right_mm    = 0.0f;

static int32_t read_left_count(void)
{
    return (int32_t)(int16_t)DL_Timer_getTimerCount(ENC_LEFT_INST);
}

static int32_t read_right_count(void)
{
    return (int32_t)(int16_t)DL_Timer_getTimerCount(ENC_RIGHT_INST);
}

void Encoder_Init(void)
{
    /* SysConfig 已经把 TIMG0 / TIMG6 配成 QEI Mode 4×。
     * 这里只做：清状态 + 启动计数器。
     */
    DL_Timer_startCounter(ENC_LEFT_INST);
    DL_Timer_startCounter(ENC_RIGHT_INST);

    /* 计数器从初值 0 开始，方向位由 QEI 自动判断 */
    s_last_left_count  = read_left_count();
    s_last_right_count = read_right_count();
    s_trip_left_mm     = 0.0f;
    s_trip_right_mm    = 0.0f;
}

void Encoder_Sample(EncoderData *out)
{
    int32_t left_now  = read_left_count();
    int32_t right_now = read_right_count();

    /* 16 bit 计数器溢出处理：差值绝对值过大 → 跨越溢出边界 */
    int32_t dl = left_now  - s_last_left_count;
    int32_t dr = right_now - s_last_right_count;

    if (dl >  32768) dl -= 65536;
    if (dl < -32768) dl += 65536;
    if (dr >  32768) dr -= 65536;
    if (dr < -32768) dr += 65536;

    s_last_left_count  = left_now;
    s_last_right_count = right_now;

    s_trip_left_mm  += (float)dl * MM_PER_PULSE;
    s_trip_right_mm += (float)dr * MM_PER_PULSE;

    if (out) {
        out->left_delta      = dl;
        out->right_delta     = dr;
        /* 速度 = 脉冲 / 周期，调用方知道周期（10ms）就能算 */
        out->left_speed_pps  = (float)dl;
        out->right_speed_pps = (float)dr;
        out->trip_left_mm    = s_trip_left_mm;
        out->trip_right_mm   = s_trip_right_mm;
    }
}

void Encoder_ResetTrip(void)
{
    s_trip_left_mm  = 0.0f;
    s_trip_right_mm = 0.0f;
    /* 不清硬件计数器，让速度采样保持连续 */
}

float Encoder_TripMM(void)
{
    return 0.5f * (s_trip_left_mm + s_trip_right_mm);
}
