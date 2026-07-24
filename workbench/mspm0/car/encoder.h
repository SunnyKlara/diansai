/*
 * encoder.h - 双编码器读取 (GPIO 中断 1x 正交解码)
 *   enc1(对应 M1): A=PA7  B=PB19      enc2(对应 M2): A=PB20 B=PB21
 *   解码: A 上升沿计一次, 读 B 电平判方向 (1x)。累计带符号计数。
 *
 * ⚠ 硬件前置(必做): 编码器 A/B 为 5V 电平, 接 MCU 前须经 1k/2k 分压降到 3.3V,
 *   否则灌坏 PA7/PB19/PB20/PB21。  // 待真机验证(编码器接好前无法验证)
 * 注: 起步用 GPIO 中断解码; 高转速下可升级 enc1 走 TIMG8 硬件 QEI。
 * counts/圈需手转实测标定(MG310P20 疑似 4x~899 => 本 1x 解码约 ~225, 以实测为准)。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

#define ENC_1  0
#define ENC_2  1

void    encoder_init(void);                 /* 使能 GPIOA/GPIOB 中断 */
int32_t encoder_count(uint8_t ch);          /* 读累计带符号计数 */
void    encoder_reset(uint8_t ch);          /* 清零计数 */

#endif /* ENCODER_H */
