/**
 * @file    feedback_control.h
 * @brief   能量回馈控制（变流器 2，发挥部分核心 30 分）
 *
 *  目标：把变流器 1 输出的三相 AC 整流回母线，损耗越小越好。
 *
 *  实现两种模式（编译开关 USE_SYNC_RECTIFIER）：
 *    模式 A（USE_SYNC_RECTIFIER=0）：
 *      硬件方案：6 个二极管三相不控整流。
 *      软件方案：本模块只做电流监测（用于 I1 ≥ 1A 验证）。
 *      Pd ≈ 17%（保拿 15 分）。
 *
 *    模式 B（USE_SYNC_RECTIFIER=1）：
 *      硬件方案：6 个 MOSFET 替代二极管，软件控制开关时机。
 *      软件方案：检测每相电流过零，控制对应 MOS 导通。
 *      Pd ≈ 5%（冲 30 分）。
 *
 *  同步整流时序（每相 360° 基波周期内）：
 *    I_phase > +SR_THRESHOLD ：上 MOS ON、下 MOS OFF
 *    I_phase < -SR_THRESHOLD ：上 MOS OFF、下 MOS ON
 *    |I_phase| < SR_THRESHOLD：上下 MOS 全 OFF（让二极管自然导通，避免反向电流）
 *
 *  调用频率：与 SVPWM 中断同频（20kHz），但实际计算开销很小。
 */

#ifndef __FEEDBACK_CONTROL_H
#define __FEEDBACK_CONTROL_H

#include <stdint.h>

typedef enum {
    SR_OFF      = 0,       /* 上下管均关闭（让 body diode 导通）*/
    SR_HIGH_ON  = 1,       /* 仅上管导通（电流为正）*/
    SR_LOW_ON   = 2,       /* 仅下管导通（电流为负）*/
} SRPhaseState;

typedef struct {
    SRPhaseState state[3]; /* 三相 SR 状态（A / B / C）*/
    float i_phase[3];      /* 三相电流采样（A）*/
    uint32_t enable_count; /* 启用同步整流的累计计数（监控用）*/
    uint8_t enabled;       /* 0=禁用（用二极管），1=启用 SR */
} FeedbackCtrl;

/**
 * @brief 初始化
 */
void FB_Init(FeedbackCtrl *fb);

/**
 * @brief 启用 / 禁用同步整流
 *  ⚠️ 必须在系统稳定（电压 RMS 已收敛）后再启用，
 *      否则启动期电流方向不确定可能导致直通烧管。
 */
void FB_Enable(FeedbackCtrl *fb, uint8_t enabled);

/**
 * @brief 周期更新（建议在 ADC DMA 完成回调或 20kHz 中断中调用）
 *  根据三相电流瞬时值更新 6 个 MOS 的状态。
 */
void FB_Update(FeedbackCtrl *fb, float ia, float ib, float ic);

/**
 * @brief 获取某相 SR 状态（供驱动层读取并写到 GPIO）
 */
SRPhaseState FB_GetState(const FeedbackCtrl *fb, uint8_t phase);

/**
 * @brief 紧急关断（故障 / 停机）
 *  立即把所有 MOS 状态置为 OFF。
 */
void FB_Disable(FeedbackCtrl *fb);

#endif /* __FEEDBACK_CONTROL_H */
