/**
 * @file    main.c
 * @brief   2024 H 自动行驶小车 主程序（MSPM0G3507）
 *
 *  状态机：
 *      ST_IDLE       上电默认，等待按键
 *      ST_CALIB_IMU  按下 START 后先做 IMU 静态零偏标定
 *      ST_RUN_TURN   原地转向段
 *      ST_RUN_LINE   直线段（IMU 闭环 + 编码器测距）
 *      ST_RUN_ARC    弧线段（红外循迹差速 PID）
 *      ST_SEG_DONE   段完成，过渡到下一段
 *      ST_DONE       任务完成（蜂鸣 + 显示总耗时）
 *      ST_LOST       异常（丢线/堵转/低压）→ 立即停车
 *
 *  时序：
 *      1ms   SysTick：累加全局 ms
 *      10ms  速度环 + 直线段控制（航向闭环）+ 编码器采样
 *      20ms  弧线段控制（循迹 PID）+ 状态机推进 + 按键扫描
 *      100ms OLED 刷新 + 电压检测
 */

#include <stdint.h>

#include "../config.h"

#include "../Drivers/motor.h"
#include "../Drivers/encoder.h"
#include "../Drivers/ir_sensor.h"
#include "../Drivers/imu_mpu6050.h"
#include "../Drivers/button.h"
#include "../Drivers/led_buzzer.h"

#include "../Algorithm/pid.h"
#include "../Algorithm/path.h"
#include "../Algorithm/line_drive.h"
#include "../Algorithm/track.h"

/* TODO_MSPM0: #include <ti/driverlib/driverlib.h> */

/* ============================================================
 *  全局状态
 * ============================================================ */
typedef enum {
    ST_IDLE = 0,
    ST_CALIB_IMU,
    ST_RUN_TURN,
    ST_RUN_LINE,
    ST_RUN_ARC,
    ST_SEG_DONE,
    ST_DONE,
    ST_LOST,
} CarState;

static volatile uint32_t g_ms = 0;     /* 全局毫秒计数（SysTick 增）*/
static CarState  g_state = ST_IDLE;
static uint8_t   g_task_id  = TASK_DEFAULT;
static uint32_t  g_task_start_ms = 0;
static uint32_t  g_task_total_ms = 0;
static uint8_t   g_calibrated = 0;

/* 速度环 PID（左右独立，增量式）*/
static PID_Controller g_pid_v_left;
static PID_Controller g_pid_v_right;

/* 当前段的速度目标（mm/s 概念，但实际我们用 PWM 单位简化）*/
static float g_tgt_v_left  = 0.0f;
static float g_tgt_v_right = 0.0f;

/* 原地转向状态（SEG_TURN）*/
static PID_Controller g_pid_turn;
static float          g_turn_setpoint_yaw = 0.0f;

/* 控制周期标志 */
static volatile uint8_t g_flag_10ms = 0;
static volatile uint8_t g_flag_20ms = 0;
static volatile uint8_t g_flag_100ms = 0;

/* ============================================================
 *  辅助函数
 * ============================================================ */
static float yaw_diff(float setpoint, float current)
{
    float e = setpoint - current;
    while (e >  180.0f) e -= 360.0f;
    while (e < -180.0f) e += 360.0f;
    return e;
}

static int16_t ramp_pwm(int16_t target, int16_t current)
{
    /* PWM 上升沿限速防打滑 */
    int16_t step = V_PWM_RAMP_PER_TICK;
    if (target > current + step) return current + step;
    if (target < current - step) return current - step;
    return target;
}

/* ============================================================
 *  状态机：进入新段
 * ============================================================ */
static void enter_segment(const Segment *seg)
{
    Encoder_ResetTrip();

    switch (seg->type) {
        case SEG_LINE:
            /* 把当前 yaw 锁为目标航向 */
            LineDrive_Begin(seg->distance_mm, IMU_GetYaw());
            g_state = ST_RUN_LINE;
            LED_On(LED_GREEN);
            LED_Off(LED_YELLOW);
            break;

        case SEG_ARC:
            Track_Begin(seg->distance_mm, seg->arc_dir);
            g_state = ST_RUN_ARC;
            LED_Off(LED_GREEN);
            LED_On(LED_YELLOW);
            break;

        case SEG_TURN:
            PID_Init(&g_pid_turn, PID_TURN_KP, PID_TURN_KI, PID_TURN_KD,
                     PID_TURN_OUT_MIN, PID_TURN_OUT_MAX);
            PID_SetTarget(&g_pid_turn, 0.0f);
            g_turn_setpoint_yaw = IMU_GetYaw() + seg->delta_yaw_deg;
            g_state = ST_RUN_TURN;
            LED_On(LED_YELLOW);
            break;

        case SEG_END:
        default:
            g_task_total_ms = g_ms - g_task_start_ms;
            g_state = ST_DONE;
            LED_Off(LED_GREEN);
            LED_Off(LED_YELLOW);
            LED_On(LED_RED);
            Buzzer_BeepBlocking(800);
            Motor_Stop();
            break;
    }
}

/* ============================================================
 *  状态机：周期推进（20ms）
 * ============================================================ */
static void state_machine_tick(void)
{
    /* 异常优先 */
    if (g_state == ST_LOST || g_state == ST_DONE || g_state == ST_IDLE) {
        Motor_Stop();
        return;
    }

    /* 按段类型分发 */
    switch (g_state) {

        case ST_RUN_TURN: {
            float yaw_err = yaw_diff(g_turn_setpoint_yaw, IMU_GetYaw());
            if ((yaw_err < 1.0f) && (yaw_err > -1.0f)) {
                /* 转到位 → 切下一段 */
                Motor_Stop();
                if (Path_Next()) {
                    enter_segment(Path_Current());
                } else {
                    enter_segment(Path_Current()); /* 进入 SEG_END 处理 */
                }
            } else {
                float diff = PID_Compute(&g_pid_turn, -yaw_err);
                /* 原地差速：左 +diff，右 -diff，禁前进 */
                g_tgt_v_left  =  diff;
                g_tgt_v_right = -diff;
            }
            break;
        }

        case ST_RUN_ARC: {
            uint8_t ir_raw[IR_NUM];
            IR_ReadAll(ir_raw);
            float trip = Encoder_TripMM();
            TrackState ts = Track_Update(ir_raw, trip,
                                         &g_tgt_v_left, &g_tgt_v_right);
            if (ts == TRACK_DONE) {
                if (Path_Next()) enter_segment(Path_Current());
                else             enter_segment(Path_Current()); /* SEG_END */
            } else if (ts == TRACK_LOST) {
                g_state = ST_LOST;
                LED_On(LED_RED);
                Buzzer_BeepBlocking(200);
            }
            break;
        }

        case ST_RUN_LINE:
            /* 直线段控制实际在 10ms 中断里跑（更高频率）*/
            if (LineDrive_IsDone()) {
                if (Path_Next()) enter_segment(Path_Current());
                else             enter_segment(Path_Current());
            }
            break;

        default: break;
    }
}

/* ============================================================
 *  10ms 控制：速度环 + 直线段航向控制
 * ============================================================ */
static void ctrl_10ms(void)
{
    /* 1. IMU 数据更新 */
    IMU_Update();

    /* 2. 编码器采样 */
    EncoderData enc;
    Encoder_Sample(&enc);

    /* 3. 直线段：高频跑航向 PID */
    if (g_state == ST_RUN_LINE) {
        LineDrive_Update(IMU_GetYaw(), Encoder_TripMM(),
                         &g_tgt_v_left, &g_tgt_v_right);
    }

    /* 4. 速度环（增量式）：把目标速度跟踪到电机 PWM
     *    简化处理：g_tgt_v_left/right 已是 PWM 单位（500 = ~50%），
     *    速度环在编码器反馈基础上做小修正。
     *    业务层用 PWM 直接喂电机，速度环作为 "调味料" 加在上面。
     */
    PID_SetTarget(&g_pid_v_left,  g_tgt_v_left);
    PID_SetTarget(&g_pid_v_right, g_tgt_v_right);
    /* 增量式：output 已经是 PWM 维度 */
    float pwm_l_f = PID_Incremental(&g_pid_v_left,  enc.left_speed_pps  * 5.0f);
    float pwm_r_f = PID_Incremental(&g_pid_v_right, enc.right_speed_pps * 5.0f);

    /* 5. ramp 防打滑 */
    static int16_t s_pwm_l = 0;
    static int16_t s_pwm_r = 0;
    s_pwm_l = ramp_pwm((int16_t)pwm_l_f, s_pwm_l);
    s_pwm_r = ramp_pwm((int16_t)pwm_r_f, s_pwm_r);

    /* 6. 题目要求只能前进：业务段强制 ≥ 0；
     *    原地转向段允许左右一正一负。
     */
    if (g_state != ST_RUN_TURN) {
        if (s_pwm_l < 0) s_pwm_l = 0;
        if (s_pwm_r < 0) s_pwm_r = 0;
    }

    /* 7. 输出 */
    if (g_state == ST_DONE || g_state == ST_LOST || g_state == ST_IDLE) {
        Motor_Stop();
        s_pwm_l = s_pwm_r = 0;
    } else {
        Motor_SetBoth(s_pwm_l, s_pwm_r);
    }
}

/* ============================================================
 *  按键处理
 * ============================================================ */
static void on_key(KeyId k)
{
    switch (k) {
        case KEY_START:
            if (g_state == ST_IDLE || g_state == ST_DONE || g_state == ST_LOST) {
                /* 装载任务 */
                Path_Load(g_task_id);
                if (!g_calibrated) {
                    g_state = ST_CALIB_IMU;
                } else {
                    g_task_start_ms = g_ms;
                    enter_segment(Path_Current());
                }
            }
            break;

        case KEY_STOP:
            Motor_Stop();
            g_state = ST_IDLE;
            LED_Off(LED_GREEN);
            LED_Off(LED_YELLOW);
            LED_Off(LED_RED);
            break;

        case KEY_TASK:
            if (g_state == ST_IDLE) {
                g_task_id = (g_task_id % 4) + 1; /* 1→2→3→4→1 */
                /* 短促鸣笛表示当前任务号 */
                for (int i = 0; i < g_task_id; i++) Buzzer_BeepBlocking(80);
            }
            break;

        case KEY_RESET:
            /* 软件复位：重新做 IMU 标定 */
            g_calibrated = 0;
            g_state = ST_IDLE;
            break;

        default: break;
    }
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    /* TODO_MSPM0: SYSCFG_DL_init();   // SysConfig 生成 */

    /* 外设初始化 */
    Motor_Init();
    Encoder_Init();
    IR_Init();
    Button_Init();
    LED_Init();
    Buzzer_Init();
    IMU_Init();

    /* 速度环 PID */
    PID_Init(&g_pid_v_left,  PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD,
             PID_SPEED_OUT_MIN, PID_SPEED_OUT_MAX);
    PID_Init(&g_pid_v_right, PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD,
             PID_SPEED_OUT_MIN, PID_SPEED_OUT_MAX);

    /* 默认任务 */
    Path_Load(g_task_id);

    LED_On(LED_GREEN);
    Buzzer_BeepBlocking(100);

    /* TODO_MSPM0: SysTick_Config(systemClock / 1000); 启动 1ms 中断 */
    /* TODO_MSPM0: 启动 PWM */

    while (1) {
        /* ---- 10ms 任务 ---- */
        if (g_flag_10ms) {
            g_flag_10ms = 0;
            ctrl_10ms();
        }

        /* ---- 20ms 任务 ---- */
        if (g_flag_20ms) {
            g_flag_20ms = 0;

            /* 状态机推进 */
            state_machine_tick();

            /* IMU 标定（在状态机调度间隙做）*/
            if (g_state == ST_CALIB_IMU) {
                if (IMU_Calibrate()) {
                    g_calibrated = 1;
                    Buzzer_BeepBlocking(200);
                    g_task_start_ms = g_ms;
                    enter_segment(Path_Current());
                } else {
                    /* 标定失败：闪红灯，让用户重按 */
                    LED_On(LED_RED);
                    g_state = ST_IDLE;
                }
            }

            /* 按键 */
            KeyId k = Button_Scan();
            if (k != KEY_NONE) on_key(k);
        }

        /* ---- 100ms 任务 ---- */
        if (g_flag_100ms) {
            g_flag_100ms = 0;
            /* TODO: OLED 刷新（任务号、状态、IMU yaw、累计耗时、电池电压）*/
            /* TODO: 电池电压检测，低于 BAT_LOW_STOP_V 时强制停车 */
        }
    }
}

/* ============================================================
 *  SysTick 中断（1ms）
 * ============================================================
 *  TODO_MSPM0: SysTick_Handler() 在 startup_*.c 里挂接到这个函数
 */
void SysTick_Handler(void)
{
    g_ms++;
    if ((g_ms %  10) == 0) g_flag_10ms  = 1;
    if ((g_ms %  20) == 0) g_flag_20ms  = 1;
    if ((g_ms % 100) == 0) g_flag_100ms = 1;
}
