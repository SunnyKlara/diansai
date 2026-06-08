/**
 * @file    main.c
 * @brief   2023 A 单相逆变器并联系统 主程序（STM32G474RET6）
 *
 *  状态机：
 *      ST_IDLE          上电待机
 *      ST_PRECHARGE     母线预充电（避免冲击）
 *      ST_RAMPUP        软启动（调制比从 MIN 渐升）
 *      ST_RUN_SINGLE    基本要求：单机逆变 + 闭环
 *      ST_RUN_PARALLEL  发挥(1)：双机并联
 *      ST_RUN_GRID_TIE  发挥(2)(3)：并网（电流源模式）
 *      ST_FAULT         异常 → 关 PWM + 蜂鸣
 *      ST_STOP          手动停机
 *
 *  时序：
 *      50μs (TIM1 中断)        SPWM 角度推进 + PWM 写入
 *      50μs (ADC DMA)          双通道采样 + RMS 累加
 *      1ms  (SysTick)          系统计时
 *      10ms                    电压外环 PI + 软启动 ramp
 *      50ms                    保护检测
 *      200ms                   OLED 刷新
 */

#include <stdint.h>

#include "../config.h"

#include "../Drivers/pwm_full_bridge.h"
#include "../Drivers/adc_sample.h"
#include "../Drivers/display.h"
#include "../Drivers/protection.h"

#include "../Algorithm/spwm.h"
#include "../Algorithm/control.h"

/* TODO_HAL: #include "stm32g4xx_hal.h" */

/* ============================================================
 *  全局
 * ============================================================ */
typedef enum {
    ST_IDLE = 0,
    ST_PRECHARGE,
    ST_RAMPUP,
    ST_RUN_SINGLE,
    ST_RUN_PARALLEL,
    ST_RUN_GRID_TIE,
    ST_FAULT,
    ST_STOP,
} SystemState;

static volatile uint32_t g_ms = 0;

static SystemState  g_state = ST_IDLE;
static TaskMode     g_task  = TASK_DEFAULT;

static volatile float g_vout_rms   = 0.0f;
static volatile float g_iout_rms   = 0.0f;
static          float g_mod_index  = MOD_INDEX_MIN;
static          float g_ramp_target = MOD_INDEX_INIT;

/* 控制周期标志 */
static volatile uint8_t g_flag_10ms  = 0;
static volatile uint8_t g_flag_50ms  = 0;
static volatile uint8_t g_flag_200ms = 0;

/* ============================================================
 *  状态切换
 * ============================================================ */
static void enter_state(SystemState s)
{
    g_state = s;
    switch (s) {
        case ST_IDLE:
        case ST_STOP:
            PWMFB_Stop();
            ADC_Sample_Stop();
            Control_Reset();
            SPWM_Reset();
            g_mod_index = MOD_INDEX_MIN;
            break;

        case ST_PRECHARGE:
            ADC_Sample_Start();
            break;

        case ST_RAMPUP:
            g_mod_index = MOD_INDEX_MIN;
            g_ramp_target = MOD_INDEX_INIT;
            Control_Init(VOUT_REF_V);   /* 闭环失能阶段 */
            SPWM_Init(SAMPLES_PER_CYCLE);
            PWMFB_Start();
            break;

        case ST_RUN_SINGLE:
        case ST_RUN_PARALLEL:
        case ST_RUN_GRID_TIE:
            /* 闭环开始接管 */
            break;

        case ST_FAULT:
            PWMFB_EmergencyOff();
            break;
    }
}

/* ============================================================
 *  TIM1 更新中断（20kHz）
 *  TODO_HAL：在 stm32g4xx_it.c 的 TIM1_UP_TIM16_IRQHandler 中调用本函数
 * ============================================================ */
void Inverter_OnTimerUpdate(void)
{
    if ((g_state != ST_RUN_SINGLE) &&
        (g_state != ST_RUN_PARALLEL) &&
        (g_state != ST_RAMPUP)) {
        return;
    }

    /* SPWM 推进 */
    float sine_val = SPWM_GetNextValue();

    /* 双极性 SPWM：A/B 桥臂占空比互补
     *   duty_a = 0.5 + 0.5 × m × sin(ωt)
     *   duty_b = 0.5 - 0.5 × m × sin(ωt)
     */
    float duty_a = 0.5f + 0.5f * g_mod_index * sine_val;
    float duty_b = 0.5f - 0.5f * g_mod_index * sine_val;

    PWMFB_SetDuty(duty_a, duty_b);
}

/* ============================================================
 *  ADC DMA 完成回调（每 1 周期 = 20ms）
 *  TODO_HAL：在 HAL_ADC_ConvCpltCallback 中调用本函数
 * ============================================================ */
void Inverter_OnADCFrameReady(void)
{
    g_vout_rms = ADC_Sample_CalcVrms();
    g_iout_rms = ADC_Sample_CalcIrms();

    /* 闭环只在 RUN 状态生效 */
    if (g_state == ST_RUN_SINGLE ||
        g_state == ST_RUN_PARALLEL ||
        g_state == ST_RUN_GRID_TIE) {
        g_mod_index = Control_VoltageLoop(g_vout_rms);
    }
}

/* ============================================================
 *  10 ms 控制：软启动
 * ============================================================ */
static void ctrl_10ms(void)
{
    if (g_state == ST_RAMPUP) {
        g_mod_index += RAMP_STEP;
        if (g_mod_index >= g_ramp_target) {
            g_mod_index = g_ramp_target;
            switch (g_task) {
                case TASK_PARALLEL: enter_state(ST_RUN_PARALLEL); break;
                case TASK_GRID_TIE: enter_state(ST_RUN_GRID_TIE); break;
                default:            enter_state(ST_RUN_SINGLE);   break;
            }
        }
    }

    if (g_state == ST_PRECHARGE) {
        /* 等 ADC 给到母线电压（实际应该有独立的母线 ADC 通道）
         * 这里简化：等 200ms 后认为预充电完成 */
        static uint32_t pre_count = 0;
        pre_count++;
        if (pre_count >= 20u) {
            pre_count = 0;
            enter_state(ST_RAMPUP);
        }
    }
}

/* ============================================================
 *  50 ms：保护
 * ============================================================ */
static void ctrl_50ms(void)
{
    if (g_state == ST_RUN_SINGLE ||
        g_state == ST_RUN_PARALLEL ||
        g_state == ST_RUN_GRID_TIE) {
        ProtStatus s = Protection_Check(g_vout_rms, g_iout_rms);
        if (s != PROT_OK) {
            enter_state(ST_FAULT);
        }
    }
}

/* ============================================================
 *  200 ms：显示
 * ============================================================ */
static void disp_200ms(void)
{
    Display_ShowString(0, 0, "2023A Inverter");

    static const char *kStateName[] = {
        "IDLE", "PRECHRG", "RAMPUP", "SINGLE", "PARALLEL", "GRIDTIE", "FAULT", "STOP"
    };
    Display_ShowString(0, 1, kStateName[g_state]);

    Display_ShowFloat(0, 2, "Vout: ", g_vout_rms, "V");
    Display_ShowFloat(0, 3, "Iout: ", g_iout_rms, "A");
    Display_ShowFloat(0, 4, "Mod : ", g_mod_index, "");
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    /* TODO_HAL: HAL_Init(); SystemClock_Config(); 各 MX_*_Init() */

    PWMFB_Init();
    ADC_Sample_Init();
    Display_Init();
    Protection_Init();
    SPWM_Init(SAMPLES_PER_CYCLE);
    Control_Init(VOUT_REF_V);

    enter_state(ST_IDLE);

    Display_Clear();
    Display_ShowString(0, 0, "2023A Ready");

    /* TODO_HAL: SysTick_Config(SystemCoreClock / 1000) → 1ms */

    /* 简化：上电直接进入预充电（实际应等按键）*/
    enter_state(ST_PRECHARGE);

    while (1) {
        if (g_flag_10ms)  { g_flag_10ms = 0;  ctrl_10ms();  }
        if (g_flag_50ms)  { g_flag_50ms = 0;  ctrl_50ms();  }
        if (g_flag_200ms) { g_flag_200ms = 0; disp_200ms(); }
    }
}

/* ============================================================
 *  SysTick 1ms
 * ============================================================ */
void SysTick_Handler(void)
{
    g_ms++;
    if ((g_ms %  10u) == 0u) g_flag_10ms  = 1;
    if ((g_ms %  50u) == 0u) g_flag_50ms  = 1;
    if ((g_ms % 200u) == 0u) g_flag_200ms = 1;
}
