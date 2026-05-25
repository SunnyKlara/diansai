/**
 * @file    protection.h
 * @brief   软件保护（过压 / 过流 / 过温）
 */

#ifndef __PROTECTION_H
#define __PROTECTION_H

#include <stdint.h>

typedef enum {
    PROT_OK = 0,
    PROT_OVP,         /* 输出过压 */
    PROT_OCP,         /* 输出过流 */
    PROT_VDC_OVP,     /* 母线过压 */
    PROT_OTP,         /* 过温 */
} ProtStatus;

void        Protection_Init(void);
ProtStatus  Protection_Check(float vout_rms, float iout_rms);
const char* Protection_GetFaultStr(ProtStatus status);

#endif /* __PROTECTION_H */
