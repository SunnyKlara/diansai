/**
 * @file    apf_compensator.h
 * @brief   APF 基波提取（滑动 DFT）+ 电流跟踪 PI
 * @note    Algorithm 层：纯算法，不依赖任何 HAL/CMSIS-DSP/驱动头文件
 */

#ifndef __APF_COMPENSATOR_H__
#define __APF_COMPENSATOR_H__

#include <stdint.h>

/* ========================================================================
 * 1. 基波提取上下文
 * ===================================================================== */

/**
 * @brief 滑动 DFT 提基波器内部状态
 *        外部代码不要直接访问字段，全部通过函数操作。
 */
typedef struct {
    float   il_buffer[400];   /* 一个工频周期 = 400 点 @ 20 kHz / 50 Hz   */
    float   sin_table[400];
    float   cos_table[400];
    uint16_t buf_idx;
    float   a1;               /* DFT sin 分量（基波幅值/相位编码）         */
    float   b1;               /* DFT cos 分量                            */
    uint8_t inited;
} apf_extractor_t;

/**
 * @brief 初始化基波提取器：预算 sin/cos 表 + 清缓冲。
 */
void apf_extractor_init(apf_extractor_t *ext);

/**
 * @brief 单点更新：吃一个新的 iL 采样，吐出当前时刻的谐波分量参考值。
 *
 * @param[in,out] ext     提取器实例
 * @param[in]     il_now  当前 iL 瞬时值（A）
 * @return                iF_ref，即 iL 减去基波后的谐波部分（A）
 *
 * @note 在 20 kHz 中断里调用，CPU 占用约 5%（O(N) DFT 全展开版本）。
 */
float apf_extractor_update(apf_extractor_t *ext, float il_now);

/* ========================================================================
 * 2. 电流跟踪 PI
 * ===================================================================== */

typedef struct {
    float kp;
    float ki;
    float ts;             /* 控制周期（秒）       */
    float integ;          /* 积分累加器           */
    float integ_max;      /* 积分限幅             */
    float duty_center;    /* 单极性 PWM 中点 0.5 */
    float duty_min;
    float duty_max;
    float ramp;           /* 软启动系数 0..1      */
    float ramp_step;      /* 每周期增量          */
} apf_pi_t;

/**
 * @brief 初始化 PI 控制器到默认参数（从 config.h 读取）
 */
void apf_pi_init(apf_pi_t *pi);

/**
 * @brief 单点 PI：参考电流 - 实际电流 → 占空比
 *
 * @param[in,out] pi
 * @param[in]     if_ref   参考电流（A）
 * @param[in]     if_act   实际电流（A）
 * @return                 PWM 占空比 [duty_min, duty_max]
 */
float apf_pi_update(apf_pi_t *pi, float if_ref, float if_act);

/**
 * @brief 启动软启 ramp（每次开 APF 调一次）
 */
void apf_pi_soft_start(apf_pi_t *pi);

/**
 * @brief 立即关 APF：积分清零 + ramp 归 0 + 占空比固定到中点
 */
void apf_pi_disable(apf_pi_t *pi);

/* ========================================================================
 * 3. 单元测试钩子（可选，仅离线编译时启用）
 * ===================================================================== */

#ifdef APF_UNIT_TEST
/**
 * @brief 直接读取内部 a1/b1，便于 PC 端 PyTest 验证。
 */
void apf_extractor_get_dft(const apf_extractor_t *ext, float *a1, float *b1);
#endif

#endif /* __APF_COMPENSATOR_H__ */
