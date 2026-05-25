/**
 * @file    bt_hc05.h
 * @brief   HC-05 蓝牙模块（UART UCA2，115200 8N1）
 *
 *  发挥(6) 手机显示用：通过手机 APP（蓝牙串口助手）接收 THD 测量数据。
 */

#ifndef __BT_HC05_H
#define __BT_HC05_H

void BT_Init(void);
void BT_SendString(const char* str);

#endif
