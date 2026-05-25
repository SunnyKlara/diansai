/**
 * @file    control.h
 * @brief   电压外环 PI 控制器（与硬件解耦）
 *
 *  原理：
 *      m(k) = m(k-1) + Kp · (e(k) - e(k-1)) + Ki · e(k)   (增量式)
 *      或者：m = m_init + Kp · e + Ki · ∫e               (位置式，本实现)
 *
 *  特性：
 *      - 含积分抗饱和（积分项被限幅到 [MOD_MIN, MOD_MAX]）
 *      - 启动时积分项预设为 MOD_INDEX_INIT，加速达到稳态
 *      - 周期 10 ms
 */

#ifndef __CONTROL_H
#define __CONTROL_H

#include <stdint.h>

/**
 * @brief 初始化（清积分项 + 设置 setpoint）
 */
void  Control_Init(float vref);

/**
 * @brief 修改 setpoint（双机并联模式可能要切换）
 */
void  Control_SetVref(float vref);

/**
 * @brief 电压闭环更新（每个正弦周期调一次，约 20 ms 即 50Hz 时）
 * @param vout_rms  实测输出电压 RMS
 * @return 调制比，已限幅到 [MOD_INDEX_MIN, MOD_INDEX_MAX]
 */
float Control_VoltageLoop(float vout_rms);

/**
 * @brief 重置积分项（故障 / 软启动重启时）
 */
void  Control_Reset(void);

#endif /* __CONTROL_H */
