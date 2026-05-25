#ifndef __SVPWM_3PHASE_H
#define __SVPWM_3PHASE_H

#include "stm32g4xx_hal.h"

typedef struct {
    float Ta, Tb, Tc;   // 三相占空比 (0~1)
    uint8_t sector;      // 扇区 (1~6)
} SVPWM_Out_t;

void SVPWM_Init(void);
SVPWM_Out_t SVPWM_Calc(float Ualpha, float Ubeta, float Udc);
void SVPWM_SetFrequency(float freq_hz);
void SVPWM_SetAmplitude(float mod_index);

// 三相逆变器顶层控制（在TIM1更新中断中调用）
void Inverter3Ph_Update(void);

#endif
