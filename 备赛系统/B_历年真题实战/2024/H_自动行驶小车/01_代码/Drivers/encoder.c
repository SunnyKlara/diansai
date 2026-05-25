/**
 * @file    encoder.c
 * @brief   编码器实现（MSPM0 TIMG6/TIMG7 编码器模式）
 */

#include "encoder.h"
#include "../config.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

static int32_t s_last_left_count  = 0;
static int32_t s_last_right_count = 0;
static float   s_trip_left_mm     = 0.0f;
static float   s_trip_right_mm    = 0.0f;

static int32_t read_left_count(void)
{
    /* TODO_MSPM0: return (int16_t)DL_Timer_getTimerCount(TIMG6); */
    return 0;
}
static int32_t read_right_count(void)
{
    /* TODO_MSPM0: return (int16_t)DL_Timer_getTimerCount(TIMG7); */
    return 0;
}
static void reset_left_count(void)  { /* TODO_MSPM0 */ }
static void reset_right_count(void) { /* TODO_MSPM0 */ }

void Encoder_Init(void)
{
    /* TODO_MSPM0:
     *  - TIMG6/TIMG7 配置为 Quadrature Encoder Mode
     *  - PA12/PA13 → TIMG6_C0/C1
     *  - PA16/PA17 → TIMG7_C0/C1
     *  - 计数模式：4x（每个边沿都计数）
     */
    s_last_left_count  = 0;
    s_last_right_count = 0;
    s_trip_left_mm     = 0.0f;
    s_trip_right_mm    = 0.0f;
}

void Encoder_Sample(EncoderData *out)
{
    int32_t left_now  = read_left_count();
    int32_t right_now = read_right_count();

    /* 16bit 计数器溢出处理：当差值绝对值过大时，认为发生了溢出 */
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
    /* 不清硬件计数器，避免影响速度采样 */
    (void)reset_left_count;
    (void)reset_right_count;
}

float Encoder_TripMM(void)
{
    /* 取左右轮平均，左右差异由控制器消除 */
    return 0.5f * (s_trip_left_mm + s_trip_right_mm);
}
