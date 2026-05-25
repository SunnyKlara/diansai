/**
 * @file    config.h
 * @brief   2024 H 自动行驶小车 全局可调参数
 * @note    所有需要调试时修改的参数都集中在这里。
 *          严禁在 .c 文件里硬编码"魔法数字"。
 *          修改 PID 参数 → 改这里 → 重新编译即可，
 *          不要去翻 algorithm/*.c。
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>

/* ============================================================
 *  1. 硬件参数（机械 / 物理常量，几乎不改）
 * ============================================================ */
#define WHEEL_DIAMETER_MM         65.0f      // 轮子直径（mm）
#define WHEEL_BASE_MM            155.0f      // 左右轮中心距（轴距，mm，实测填）
#define ENCODER_PPR             1320         // 编码器每圈脉冲（含 4 倍频和 30 减速）
#define MM_PER_PULSE              (3.14159265f * WHEEL_DIAMETER_MM / (float)ENCODER_PPR)
                                              // ≈ 0.1547 mm/脉冲

/* ============================================================
 *  2. 任务路径常量（赛场实测后修改）
 * ============================================================ */
#define PATH_LINE_AB_MM        1000.0f       // A→B 直线长度（场地实测）
#define PATH_LINE_AC_MM        1280.6f       // A→C 对角线（理论 √(100²+80²)，实测可能略短）
#define PATH_ARC_MM            1256.6f       // 半圆弧长 = π × R(40cm) = 125.66cm
#define PATH_TURN_AC_DEG         38.66f      // A→C 起步原地转角（理论 atan(80/100) = 38.66°）
#define PATH_TURN_BD_DEG        -38.66f      // B→D 反向

#define ARC_END_TOL_MM            50.0f      // 弧线段到达终点容差（弧长方向）

/* ============================================================
 *  3. PID 参数（核心调参点）
 * ============================================================ */

/* ---- 速度环（增量式）---- */
#define PID_SPEED_KP             10.0f
#define PID_SPEED_KI              1.0f
#define PID_SPEED_KD              0.0f
#define PID_SPEED_OUT_MIN     -999.0f
#define PID_SPEED_OUT_MAX      999.0f

/* ---- 航向环（位置式，直线段使用）---- */
#define PID_YAW_KP                3.0f
#define PID_YAW_KI                0.05f
#define PID_YAW_KD                1.5f
#define PID_YAW_OUT_MIN        -300.0f       // 速度差最大 ±300（PWM 单位）
#define PID_YAW_OUT_MAX         300.0f

/* ---- 循迹环（位置式，弧线段使用）---- */
#define PID_TRACK_KP              8.0f
#define PID_TRACK_KI              0.0f
#define PID_TRACK_KD              3.0f
#define PID_TRACK_OUT_MIN      -500.0f
#define PID_TRACK_OUT_MAX       500.0f

/* ---- 原地转向（位置式）---- */
#define PID_TURN_KP               4.0f
#define PID_TURN_KI               0.0f
#define PID_TURN_KD               2.0f
#define PID_TURN_OUT_MIN       -400.0f
#define PID_TURN_OUT_MAX        400.0f

/* ============================================================
 *  4. 速度档位（PWM / 编码器目标）
 * ============================================================ */
#define MOTOR_PWM_MAX           999          // 对应 PWM 计数器 ARR
#define V_LINE_PWM              500          // 直线段基础 PWM（≈ 50%）
#define V_ARC_PWM               350          // 弧线段基础 PWM
#define V_LINE_END_PWM          250          // 直线末段减速 PWM
#define V_TURN_PWM              300          // 原地转向 PWM
#define V_DECEL_DIST_MM         150.0f       // 直线段末段减速距离

#define V_PWM_RAMP_PER_TICK      30          // 每 10ms PWM 增量上限（防打滑）

/* ============================================================
 *  5. 红外循迹传感器
 * ============================================================ */
#define IR_NUM                  5
/* 5 路传感器从左到右的位置权重，单位任意（最右 - 最左 = 4 单位）*/
#define IR_WEIGHT_0           (-2)           // 最左
#define IR_WEIGHT_1           (-1)
#define IR_WEIGHT_2            (0)           // 中央
#define IR_WEIGHT_3            (1)
#define IR_WEIGHT_4            (2)           // 最右

#define IR_LOST_TIMEOUT_MS    1000           // 丢线 1 秒后停车

/* 弧线起点检测：中央传感器从白→黑且至少 2 个传感器同时压线 */
#define IR_ARC_START_MIN       2
/* 弧线终点检测：连续 N 个周期所有传感器都白 */
#define IR_ARC_END_TICKS      10             // 10×20ms = 200ms 全白

/* ============================================================
 *  6. IMU
 * ============================================================ */
#define IMU_I2C_ADDR          0x68           // MPU6050 默认地址
#define IMU_CALIB_SAMPLES      200           // 静态零偏标定采样数
#define IMU_GYRO_DEADBAND      0.5f          // °/s 死区（小于此值视为静止）
#define IMU_DRIFT_PER_SEC      0.05f         // 期望陀螺漂移上限（°/s）

/* ============================================================
 *  7. 系统时序
 * ============================================================ */
#define SYS_HEARTBEAT_MS         1
#define CTRL_SPEED_MS           10           // 速度环 + 航向环周期
#define CTRL_TRACK_MS           20           // 循迹环 + 状态机周期
#define DISPLAY_PERIOD_MS      100

/* ============================================================
 *  8. 安全保护
 * ============================================================ */
#define BAT_LOW_WARN_V           6.8f        // 低压警告
#define BAT_LOW_STOP_V           6.5f        // 强制停车
#define MOTOR_STALL_TIMEOUT_MS  500          // 堵转检测：高 PWM 但编码器无变化
#define OUT_OF_BOUND_MM       2000.0f        // 推算位置超出场地阈值

/* ============================================================
 *  9. 任务选择
 * ============================================================ */
typedef enum {
    TASK_1_AB_LINE = 1,
    TASK_2_LOOP    = 2,
    TASK_3_CROSS   = 3,
    TASK_4_FOUR_LAPS = 4,
} TaskID;

#define TASK_DEFAULT  TASK_2_LOOP

#endif /* __CONFIG_H */
