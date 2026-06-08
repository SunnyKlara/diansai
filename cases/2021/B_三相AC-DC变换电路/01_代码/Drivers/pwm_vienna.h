/**
 * @file    pwm_vienna.h
 * @brief   Vienna 整流器三路独立 PWM（单极性）+ Buck 互补 PWM
 */
#ifndef __PWM_VIENNA_H__
#define __PWM_VIENNA_H__

#include <stdint.h>

void pwm_init(void);
void pwm_vienna_start(void);
void pwm_vienna_stop(void);
void pwm_vienna_set(float da, float db, float dc);

void pwm_buck_start(void);
void pwm_buck_stop(void);
void pwm_buck_set(float duty);

void pwm_emergency_off(void);

#endif
