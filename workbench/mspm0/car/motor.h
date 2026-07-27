/*
 * motor.h - DRV8231 双电机驱动 (天猛星, TIMA0 双路PWM/电机)
 * M1: IN1=PA8(TIMA0_C0)  IN2=PA9(TIMA0_C1)
 * M2: IN1=PB12(TIMA0_C2) IN2=PB13(TIMA0_C3)
 * 双向: 正转 IN1=PWM/IN2=0; 反转 IN1=0/IN2=PWM; 停=两路都0(滑行)。
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>
#include "config.h"   /* MOTOR_PWM_PERIOD / CUR_AVG_N / CUR_MA_DIV 等参数集中在此 */

#define MOTOR_M1  0
#define MOTOR_M2  1

void motor_init(void);                        /* 启动PWM,占空归0(停) */
void motor_set(uint8_t ch, int16_t duty);     /* duty: -100..100, >0前进 <0后退 0滑行 */
void motor_stop_all(void);                    /* 两电机滑行停 */

/* 读两路电机电流 ADC 原始值(0..4095, DRV8231 IPROPI @ ADC0 序列采样)。
 * 内部先 enableConversions 再等转换完成。IPROPI 脚未接则读到噪声/0。 */
void motor_read_current_raw(uint16_t *m1_raw, uint16_t *m2_raw);

/* 同上, 但一次取回**整条 ADC 序列**的三路: 两路电机 + 电磁铁(PA24)。任一指针可为 NULL。
 * ADC 只有一个、序列一次触发全部转换 ⇒ **触发点必须只有这一处**, 电磁铁不许自己去 startConversion
 * (两边互相打断对方的序列, 症状是读数偶发串到别的通道上, 很难查)。 */
void motor_adc_read_all(uint16_t *m1_raw, uint16_t *m2_raw, uint16_t *mag_raw);

/* ADC 原始值 -> 电机电流(mA)。定标: IPROPI≈1575uA/A + R≈680Ω + Vref 3.3V。
 * I(mA) ≈ raw*3300/(4096*1.071) = raw*3300/4387。R 因模块批次不同, 需实测校准。 */
int32_t motor_current_ma(uint16_t raw);

#endif /* MOTOR_H */
