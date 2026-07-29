/*
 * encoder.h - 双编码器读取: **定时采样 4x 正交解码**(不使能任何 GPIO 中断)
 *   通道映射: enc1 <-> M1, enc2 <-> M2。累计带符号计数, 前进为正。
 *   引脚不在本文复述 -> 真值见 .kiro/steering/工程事实SSOT.md 的 §B(编码器两行);
 *   代码级真源是 car.syscfg 生成的 ti_msp_dl_config.h。
 *
 * ⚠ 为什么是"采样"而不是"边沿中断"(真机血泪, 90c1c8c 起改):
 *   边沿中断版在电机 EMI 下把一次真跳变数成几千次(ISR 计数比软件轮询高 5~6 个数量级),
 *   计数完全不可信。采样解码只在固定时刻读 A/B 电平, 落在采样间隔里的毛刺被直接丢弃。
 *   外部上拉只能让沿变陡, 对电机 EMI 的快毛刺无效 -> 采样才是这个环境下的正解。
 *
 * ⚠ 硬件前置的正解 = 编码器**供 3.3V** + A/B 各**上拉到 3.3V**(开集/OC 输出)。
 *   ⛔ 绝不要加对地分压电阻 —— 本文旧版曾写"须经 1k/2k 分压降到 3.3V", 照做的结果是
 *   信号被那条对地腿钳死、整天一个数都不计。该说法已作废, 别再复活它。
 *
 * ⚠ counts/圈 见 config.h 的 ENC_CPR(真机手转标定值; 换电机/换编码器必重标)。
 *   本文旧版写死过"约 ~225", 与真值差约 4 倍 —— 所以这里不再写任何数字。
 *
 * 已知失败模式(诊断用, 2026-07-27 真机踩过):
 *   转速读数远低于实际、但计数单调递增且静止漂移为 0  =>  **漏采**, 去查上拉(虚焊过一次)。
 *   "计数单调" 和 "静止漂移 0" 这两个检查都拦不住漏采 -> 判编码器可信只能靠**手转实测**
 *   (tools/enc_delta.ps1: 只转一侧另一侧必须为 0 / 两轮前进同号 / counts per rev 对得上)。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "config.h"   /* CFG_ENC_SIGN_L / CFG_ENC_SIGN_R (板级线束朝向, §2.1) */

#define ENC_1  0
#define ENC_2  1

void    encoder_init(void);                 /* 初始化状态(采样式解码,不使能中断) */
void    encoder_poll(void);                 /* 采一次A/B按正交状态机累加;须固定速率周期调用(主循环每tick) */
int32_t encoder_count(uint8_t ch);          /* 读累计带符号计数 */
void    encoder_reset(uint8_t ch);          /* 清零计数 */

#endif /* ENCODER_H */
