#ifndef __CONFIG_H__
#define __CONFIG_H__

/**
 * @file    config.h
 * @brief   2025 A 能量回馈变流器 —— 全局可调参数集中管理
 * @note    所有可调参数（PID、采样率、死区、保护阈值等）必须集中在此处。
 *          调试时只改这里，不要散落到各个 .c 文件。
 */

/* ================== 母线 / 输出指标 ================== */
#define VDC_NOMINAL_V              48.0f      /* 直流母线标称电压 (V) */
#define VDC_MAX_V                  55.0f      /* 母线过压阈值 (V) */
#define VDC_MIN_V                  40.0f      /* 母线欠压阈值 (V) */

#define VLINE_RMS_REF_V            32.0f      /* 输出线电压 RMS 目标 (V) */
#define VLINE_RMS_TOLERANCE_V       0.25f     /* 容差 ±0.25V (题目要求) */

#define IOUT_RATED_RMS_A            2.0f      /* 输出额定相电流 RMS (A) */
#define IOUT_OVERCURRENT_A          4.0f      /* 过流保护阈值 (A) */

#define FREQ_DEFAULT_HZ            50.0f      /* 默认输出频率 (Hz) */
#define FREQ_MIN_HZ                20.0f      /* 频率可调下限 */
#define FREQ_MAX_HZ               100.0f      /* 频率可调上限 */
#define FREQ_STEP_HZ                1.0f      /* 频率步进 */

/* ================== PWM / SVPWM ================== */
#define PWM_FSW_HZ              20000U        /* 开关频率 20 kHz */
#define PWM_DEADTIME_NS           800U        /* 死区时间 (ns)，调参重点 */
#define PWM_MIN_DUTY                0.05f     /* 最小占空比限位 */
#define PWM_MAX_DUTY                0.95f     /* 最大占空比限位 */

/* ================== ADC 采样 ================== */
#define ADC_FS_HZ               20000U        /* ADC 采样率 = PWM 频率 */
#define ADC_OVERSAMPLE_BITS         4U        /* 过采样位数（G474 16bit 增强分辨率）*/

/* ADC 校准（实测后填入）*/
#define ADC_VREF_V                  3.3f
#define ADC_RESOLUTION_BITS        12U

/* 电压采样分压比：U_real = ADC_voltage * V_DIV_RATIO */
#define V_DIV_RATIO_PHASE          21.0f      /* 相电压分压比，需实测标定 */
#define V_DIV_RATIO_DC             21.0f      /* 母线电压分压比 */

/* 电流采样：ACS712-20A，灵敏度 100mV/A，零点偏置 1.65V */
#define I_SENSE_SENSITIVITY_V_PER_A 0.100f
#define I_SENSE_OFFSET_V            1.65f

/* ================== 电压外环 PI ================== */
#define VLOOP_TS_S                  0.010f    /* 电压环周期 10 ms */
#define VLOOP_KP                    0.005f    /* 比例（调参起点）*/
#define VLOOP_KI                    0.0001f   /* 积分（调参起点）*/
#define VLOOP_OUT_MAX               0.95f     /* 调制比上限 */
#define VLOOP_OUT_MIN               0.05f     /* 调制比下限 */
#define VLOOP_INT_MAX               0.5f      /* 积分饱和上限（防积分饱和）*/
#define VLOOP_INT_MIN              -0.5f

/* 调制比前馈：m_ff = Vref * sqrt(2) / (Vdc / sqrt(3)) */
#define MODULATION_FEEDFORWARD_EN   1U        /* 1=开启前馈 */

/* ================== 能量回馈 / 同步整流 ================== */
#define FEEDBACK_ENABLE             1U        /* 1=启用同步整流（发挥部分）*/
#define SYNC_RECT_DEADTIME_US       5U        /* 同步整流死区 (μs) */
#define SYNC_RECT_ZERO_THRESH_A     0.15f     /* 电流过零阈值，<此值用二极管自然导通 */

/* ================== 系统状态 / 故障保护 ================== */
#define PRECHARGE_TIMEOUT_MS      500U        /* 母线预充电超时 */
#define SOFTSTART_RAMP_MS        2000U        /* 软启动时间 */
#define FAULT_LATCH_EN              1U        /* 1=故障锁存（需按键复位）*/

/* ================== 任务周期（SysTick 节拍 = 1ms）================== */
#define TASK_VOLTAGE_LOOP_MS       10U
#define TASK_FAULT_CHECK_MS        50U
#define TASK_OLED_REFRESH_MS      200U
#define TASK_BUTTON_SCAN_MS        20U

/* ================== UI / OLED ================== */
#define OLED_I2C_ADDR              0x78
#define OLED_WIDTH                  128
#define OLED_HEIGHT                  64

/* ================== 调试 / 串口 ================== */
#define DEBUG_UART_BAUD          115200U
#define DEBUG_LOG_EN                1U        /* 1=输出调试日志 */

#endif /* __CONFIG_H__ */
