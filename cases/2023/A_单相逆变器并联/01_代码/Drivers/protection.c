/**
 * @file    protection.c
 * @brief   软件保护实现
 */

#include "protection.h"
#include "../config.h"

static uint8_t s_ovp_cnt = 0;
static uint8_t s_ocp_cnt = 0;

void Protection_Init(void)
{
    s_ovp_cnt = 0;
    s_ocp_cnt = 0;
}

ProtStatus Protection_Check(float vout_rms, float iout_rms)
{
    if (vout_rms > VOUT_OVP_THRESHOLD) {
        if (++s_ovp_cnt >= PROT_CONFIRM_COUNT) return PROT_OVP;
    } else {
        s_ovp_cnt = 0;
    }

    if (iout_rms > IOUT_OCP_THRESHOLD) {
        if (++s_ocp_cnt >= PROT_CONFIRM_COUNT) return PROT_OCP;
    } else {
        s_ocp_cnt = 0;
    }

    return PROT_OK;
}

const char* Protection_GetFaultStr(ProtStatus status)
{
    switch (status) {
        case PROT_OVP:     return "FAULT: OVP";
        case PROT_OCP:     return "FAULT: OCP";
        case PROT_VDC_OVP: return "FAULT: VDC OVP";
        case PROT_OTP:     return "FAULT: OTP";
        default:           return "OK";
    }
}
