/**
 * 保护模块
 * 过压(OVP)、过流(OCP)保护
 * 
 * 设计思路：
 *   - 软件保护：在主循环中周期性检查，响应慢但灵活
 *   - 硬件保护(推荐加上)：比较器+SR锁存器直接关断PWM，响应<1μs
 *   - 本文件实现软件保护，硬件保护需要额外电路
 */

#include "protection.h"

static uint8_t ovp_count = 0;  // 连续过压计数（防误触发）
static uint8_t ocp_count = 0;

#define PROT_CONFIRM_COUNT  3  // 连续3次超限才触发保护

void Protection_Init(void)
{
    ovp_count = 0;
    ocp_count = 0;
}

/**
 * 保护检测（在主循环中每100ms调用一次）
 * @return PROT_OK=正常, 其他=故障类型
 */
ProtStatus_t Protection_Check(float vout_rms, float iout_rms)
{
    // 输出过压检测
    if (vout_rms > VOUT_OVP_THRESHOLD) {
        ovp_count++;
        if (ovp_count >= PROT_CONFIRM_COUNT) return PROT_OVP;
    } else {
        ovp_count = 0;
    }
    
    // 输出过流检测
    if (iout_rms > IOUT_OCP_THRESHOLD) {
        ocp_count++;
        if (ocp_count >= PROT_CONFIRM_COUNT) return PROT_OCP;
    } else {
        ocp_count = 0;
    }
    
    return PROT_OK;
}

const char* Protection_GetFaultStr(ProtStatus_t status)
{
    switch (status) {
        case PROT_OVP:     return "FAULT: OVP";
        case PROT_OCP:     return "FAULT: OCP";
        case PROT_VDC_OVP: return "FAULT: VDC OVP";
        case PROT_OTP:     return "FAULT: OTP";
        default:           return "OK";
    }
}
