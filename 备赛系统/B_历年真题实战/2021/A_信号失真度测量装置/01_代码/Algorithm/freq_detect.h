/**
 * 频率检测模块 —— 头文件
 * 
 * 【为什么频率检测是这道题的第一步？】
 * 
 * 同步采样需要知道信号频率才能计算采样率。
 * 但频率检测本身也需要采样数据。
 * 这是一个鸡生蛋的问题。
 * 
 * 解决：两步法
 * 1. 先用固定采样率(100kHz)采集一帧，用过零检测粗测频率
 * 2. 根据粗测频率计算同步采样率，重新采集
 * 
 * 粗测的精度要求：
 * 同步采样率 = freq × N / M
 * 如果freq有1%的误差，同步采样率也有1%误差
 * 这会导致FFT中基频偏离bin中心约0.01个bin
 * 频谱泄漏引起的幅度误差约0.1% → 可以接受
 * 
 * 所以过零检测的1%精度就够了。
 */

#ifndef __FREQ_DETECT_H
#define __FREQ_DETECT_H

#include "../config.h"

/**
 * 过零检测法测量信号频率
 * 
 * @param data  有符号采样数据（去偏置后）
 * @param len   数据长度
 * @param fs    采样率(Hz)
 * @return 信号频率(Hz)，0表示检测失败
 * 
 * 精度：约0.5~1%（取决于采样率和信号周期数）
 * 对于100kHz采样率、1kHz信号、1024个点：
 *   包含约10个周期，9个过零间隔
 *   频率 = 9 / (9个间隔的总时间)
 *   时间分辨率 = 1/100kHz = 10μs
 *   1kHz周期 = 1000μs
 *   精度 ≈ 10μs/1000μs = 1% → 够用
 */
float FreqDetect_Measure(int16_t* data, uint16_t len, float fs);

/**
 * 计算同步采样率
 * 
 * @param freq      信号频率(Hz)
 * @param fs_init   初始采样率(Hz)
 * @return 同步采样率(Hz)
 * 
 * 使得FFT_SIZE个采样点恰好覆盖M个完整周期。
 * M选择使同步采样率最接近初始采样率的值。
 */
float FreqDetect_CalcSyncRate(float freq, float fs_init);

#endif
