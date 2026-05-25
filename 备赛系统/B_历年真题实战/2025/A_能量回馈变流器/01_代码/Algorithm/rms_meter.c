/**
 * @file    rms_meter.c
 * @brief   滑动窗口 RMS 实现
 */

#include "rms_meter.h"
#include <math.h>
#include <string.h>

void RMS_Init(RMSMeter *m)
{
    if (!m) return;
    memset(m->sample_sq, 0, sizeof(m->sample_sq));
    m->idx = 0;
    m->sum_sq = 0.0f;
    m->rms = 0.0f;
    m->ready = 0;
    m->fill_count = 0;
}

float RMS_Update(RMSMeter *m, float sample)
{
    if (!m) return 0.0f;

    float sq = sample * sample;

    /* 增量更新平方和：减去最老的，加入最新的 */
    m->sum_sq -= m->sample_sq[m->idx];
    m->sample_sq[m->idx] = sq;
    m->sum_sq += sq;

    /* 防止累积浮点漂移：sum_sq 不应为负 */
    if (m->sum_sq < 0.0f) m->sum_sq = 0.0f;

    m->idx = (m->idx + 1u) % RMS_BUFFER_LEN;

    if (!m->ready) {
        m->fill_count++;
        if (m->fill_count >= RMS_BUFFER_LEN) {
            m->ready = 1u;
        }
    }

    /* 计算 RMS */
    if (m->ready) {
        m->rms = sqrtf(m->sum_sq / (float)RMS_BUFFER_LEN);
    } else {
        /* 尚未填满窗口时按已采集的点数计算（启动期渐近）*/
        m->rms = sqrtf(m->sum_sq / (float)m->fill_count);
    }

    return m->rms;
}

float RMS_Get(const RMSMeter *m)
{
    return (m) ? m->rms : 0.0f;
}

void RMS_Reset(RMSMeter *m)
{
    RMS_Init(m);
}
