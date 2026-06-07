/**
 * @file    config.h
 * @brief   {{YEAR}} {{PROBLEM}} 集中配置
 *
 * 所有"可能要调"的常量都集中放在这里：
 *   - 控制环路系数（PID）
 *   - 采样率 / 滤波参数
 *   - 阈值 / 限幅
 *   - 引脚 / 通道映射
 *
 * 业务代码禁止散落 magic number。
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ===== 系统时钟与采样 ===== */
#define SYSTEM_CLOCK_HZ         (170000000U)
#define ADC_SAMPLE_RATE_HZ      (10000U)
#define CONTROL_LOOP_HZ         (1000U)

/* ===== PID 默认参数 ===== */
#define PID_KP_DEFAULT          (1.0f)
#define PID_KI_DEFAULT          (0.0f)
#define PID_KD_DEFAULT          (0.0f)
#define PID_OUT_LIMIT           (1000.0f)

/* ===== 阈值与限幅 ===== */
#define THRESHOLD_TODO          (0.0f)

/* ===== 调试开关 ===== */
#define DEBUG_UART_ENABLE       (1)
#define DEBUG_LED_ENABLE        (1)

#endif /* CONFIG_H */
