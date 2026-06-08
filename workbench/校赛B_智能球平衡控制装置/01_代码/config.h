/**
 * config.h —— 校赛B 智能球平衡控制装置 集中可调参数
 *
 * 所有可调参数集中此处（steering 规范）。
 * 控制参数已由 PC 金标准 tests/ball_plant_golden.py 验证：
 *   定高 10/15/20cm 稳态误差 0.13/0.21/0.32cm；10↔20cm 切换 0.8/0.96s；抗扰自恢复。
 * 平台：STM32H750 @480MHz。算法层(Algorithm/)不依赖 HAL，可 PC 验证。
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ============ 控制节拍 ============ */
#define CTRL_HZ            50          /* 控制频率 Hz（球振荡≤2Hz，10×余量足够）*/
#define CTRL_TS_MS         20          /* 控制周期 ms = 1000/CTRL_HZ */
#define CTRL_TS_F          0.02f       /* 控制周期 s（浮点）*/

/* ============ 高度量程 / 目标 ============ */
#define H_MIN_CM           3.0f        /* 测量下限 */
#define H_MAX_CM           40.0f       /* 测量上限 */
#define H_PRESET1_CM       10.0f       /* 按键预设 1 */
#define H_PRESET2_CM       15.0f       /* 按键预设 2 */
#define H_PRESET3_CM       20.0f       /* 按键预设 3 */
#define H_STEP_CM          1.0f        /* 任意高度按键步进 */

/* ============ PID（PC 金标准验证值，按 cm 量纲）============ */
#define PID_KP             0.55f
#define PID_KI             0.18f
#define PID_KD             0.85f       /* 微分先行：对测量微分 */
#define PID_I_LIMIT        0.25f       /* 积分项限幅（占空比单位）*/
#define PID_DEADBAND_CM    0.2f        /* 误差死区：|e|<0.2cm 不积分（抗稳态爬行）*/

/* ============ 设定值 ramp（满足"单次≤3cm""无明显振荡"）============ */
#define SET_RAMP_CM_PER_TICK  6.0f     /* 每控制周期目标最多变化 cm（注：cm/0.02s）*/

/* ============ 风机 PWM（TIM8_CH2, PI6, 2kHz）============ */
#define FAN_PWM_ARR        1000        /* 占空比分辨率（0~1000）*/
#define FAN_U_MIN          0.20f       /* 起浮死区：低于此不足以托球 */
#define FAN_U_MAX          1.00f       /* 占空比上限 */
/* 前馈基准占空比由 u_feedforward(h) 在代码中按风道标定参数算出，
   标定参数（需上板用电位器/串口实测校准后回填）： */
#define FAN_K              16.0f       /* 满占空比出口风速标度 m/s（标定）*/
#define FAN_ALPHA          3.0f        /* 风速随高度衰减系数 1/m（标定）*/

/* ============ 传感器 ============ */
/* 选 1：保底用 HC-SR04（练手工程已实现）；选 0：主用 VL53L1X TOF */
#define SENSOR_USE_HCSR04  1
#define SENSOR_MEDIAN_N    5           /* 中值滤波窗口 */
#define SENSOR_JUMP_LIMIT_CM 3.0f      /* 单帧跳变>此值视为坏点剔除 */
#define SOUND_TEMP_COMP    1           /* 超声声速温补开关 */

/* ============ 显示（OLED SSD1306 128x64）============ */
#define DISP_REFRESH_MS    100         /* 数值/曲线刷新 10Hz */
#define CURVE_X0           20          /* 曲线区左边界像素 */
#define CURVE_W            104         /* 曲线区宽 */
#define CURVE_Y0           16          /* 曲线区上边界 */
#define CURVE_H            44          /* 曲线区高 */
#define CURVE_WIN_S        10          /* 横轴时间窗 s */

/* ============ 状态机 ============ */
#define SOFTSTART_MS       1500        /* 软启 ramp 到 u_ff 时长（启动≤5s 约束）*/
#define FAULT_LOST_MS      1000        /* 传感器丢失>此时长进 FAULT */

#endif /* CONFIG_H */
