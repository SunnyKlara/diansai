#ifndef __PROTECTION_H
#define __PROTECTION_H

#include "stm32g4xx_hal.h"

#define VOUT_OVP_THRESHOLD   30.0f   // 过压保护阈值 (Vrms)
#define IOUT_OCP_THRESHOLD   3.0f    // 过流保护阈值 (Arms)
#define VDC_OVP_THRESHOLD    55.0f   // 母线过压阈值 (V)
#define TEMP_OTP_THRESHOLD   80.0f   // 过温保护阈值 (℃, 预留)

typedef enum {
    PROT_OK = 0,
    PROT_OVP,       // 输出过压
    PROT_OCP,       // 输出过流
    PROT_VDC_OVP,   // 母线过压
    PROT_OTP        // 过温
} ProtStatus_t;

void Protection_Init(void);
ProtStatus_t Protection_Check(float vout_rms, float iout_rms);
const char* Protection_GetFaultStr(ProtStatus_t status);

#endif
