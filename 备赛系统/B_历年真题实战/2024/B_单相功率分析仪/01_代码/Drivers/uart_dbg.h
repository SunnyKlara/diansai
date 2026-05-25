/**
 * @file    uart_dbg.h
 * @brief   UART 调试通道（115200 8N1）
 *
 *  用途（按重要性排序）：
 *      1. 校准命令通道：PC 发送 "CAL V=220.0 I=2.20\r\n" → 装置反推系数存 FRAM
 *      2. 实时数据查看："STAT\r\n" → 装置回 Vrms/Irms/P/PF/THD
 *      3. 原始采样导出："DUMP\r\n" → 装置吐 1000 对 (V_adc, I_adc) 让 PC 跑金标准
 *      4. 出厂复位："RST\r\n" → 清校准回初值
 *
 *  低功耗注意：
 *      不工作时 UART 进入 hold（不消耗），有命令时才唤醒。
 *      绝对不要持续打印 → 持续 active 破 50mW 预算。
 *
 *  TODO_TI：
 *      MSP430FR5994 LaunchPad eUSCI_A0 → P2.0(TXD) / P2.1(RXD) → USB 板载串口
 */

#ifndef __UART_DBG_H
#define __UART_DBG_H

#include <stdint.h>

void UART_Init(uint32_t baud);
void UART_PutChar(char c);
void UART_PutString(const char *s);
void UART_Printf(const char *fmt, ...);

/** @brief 主循环里轮询调用，处理积压的命令
 *  @note  内部调用 Calib_Save / ADCDual_GetRawBuffer 等。
 */
void UART_PollCommands(void);

#endif /* __UART_DBG_H */
