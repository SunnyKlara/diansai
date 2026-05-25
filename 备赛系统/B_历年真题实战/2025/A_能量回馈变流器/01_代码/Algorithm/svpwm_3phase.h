/**
 * @file    svpwm_3phase.h
 * @brief   三相空间矢量调制（SVPWM）算法
 *
 *  本模块与硬件解耦：
 *      - 不依赖任何 HAL 库
 *      - 不直接操作寄存器
 *      - 通过 PWM3Phase_SetDuty(Ta, Tb, Tc) 写入到底层（在 main 中调用）
 *
 *  包含：
 *      - SVPWM 七段式核心算法（扇区判断 + 矢量时间 + 占空比分配）
 *      - 角度推进（基波频率可配）
 *      - 调制比限幅
 */

#ifndef __SVPWM_3PHASE_H
#define __SVPWM_3PHASE_H

#include <stdint.h>

typedef struct {
    float   Ta, Tb, Tc;     /* 三相占空比 [0, 1] */
    uint8_t sector;         /* 扇区 1~6 */
} SVPWM_Out;

void SVPWM_Init(void);

/**
 * @brief 通用 SVPWM 计算（输入 αβ 坐标系电压矢量）
 */
SVPWM_Out SVPWM_Calc(float Ualpha, float Ubeta, float Udc);

/**
 * @brief 设置基波输出频率
 * @param freq_hz  20.0 ~ 100.0 Hz
 */
void SVPWM_SetFrequency(float freq_hz);

/**
 * @brief 设置调制比 m （线性范围 0.10 ~ 0.95）
 */
void SVPWM_SetAmplitude(float mod_index);

/**
 * @brief 设置母线电压（每次电压变化时调用，用于 SVPWM 矢量幅值计算）
 */
void SVPWM_SetVdc(float vdc);

/**
 * @brief 三相逆变器更新（在 TIM1 更新中断中调用，20kHz）
 *
 *  调用链：
 *      推进电角度 → 计算 Uα/Uβ → SVPWM_Calc → PWM3Phase_SetDuty
 */
void Inverter3Ph_Update(void);

#endif /* __SVPWM_3PHASE_H */
