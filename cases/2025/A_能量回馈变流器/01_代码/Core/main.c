/**
 * @file    main.c
 * @brief   2025 A 能量回馈变流器 主程序（STM32G474RET6）
 *
 *  状态机：
 *      ST_IDLE          上电默认，等待 START
 *      ST_PRECHARGE     母线预充电（避免大电流冲击）
 *      ST_RAMPUP        软启动：调制比从 0.1 升到稳态
 *      ST_RUN_INVERTER  基本要求：三相逆变 + 电压闭环
 *      ST_RUN_FEEDBACK  发挥要求：能量回馈（含同步整流）
 *      ST_FAULT         异常（过压/过流/温度）→ 关 PWM + 蜂鸣
 *      ST_STOP          手动停机
 *
 *  时序：
 *      50μs (TIM1 中断)   SVPWM 角度更新 + PWM 写入
 *      50μs (ADC DMA)     三相 ADC 完成回调 → RMS 累加 + SR 决策
 *      1ms  (SysTick)     系统计时
 *      10ms                电压外环 PI + 频率/电压设定更新
 *      50ms                故障检测
 *      200ms               OLED 刷新 + 按键扫描
 */

#include <stdint.h>
#include <math.h>

#include "../config.h"

#include "../Drivers/adc_3phase.h"
#include "../Drivers/pwm_3phase.h"
#include "../Drivers/oled.h"
#include "../Drivers/button.h"
#include "../Drivers/led_buzzer.h"

#include "../Algorithm/svpwm_3phase.h"
#include "../Algorithm/voltage_loop.h"
#include "../Algorithm/rms_meter.h"
#include "../Algorithm/feedback_control.h"

/* ============================================================
 *  全局状态
 * ============================================================ */
typedef enum {
    ST_IDLE = 0,
    ST_PRECHARGE,
    ST_RAMPUP,
    ST_RUN_INVERTER,
    ST_RUN_FEEDBACK,
    ST_FAULT,
    ST_STOP,
} SystemState;

static volatile uint32_t g_ms = 0;

static SystemState  g_state         = ST_IDLE;
static TaskMode     g_task          = TASK_DEFAULT;
static uint32_t     g_state_enter_ms = 0;

static float        g_freq_set      = FREQ_OUT_DEFAULT_HZ;
static float        g_mod_index     = MOD_INDEX_INIT;
static float        g_ramp_target   = MOD_INDEX_INIT;
static float        g_vbus_meas     = VDC_NOMINAL_V;

/* 算法对象 */
static VoltageLoop  g_vloop;
static RMSMeter     g_vline_rms;       /* 用 Vab 估线电压 RMS */
static RMSMeter     g_iphase_rms;      /* 用 Ia 估线电流 RMS */
static FeedbackCtrl g_fb;

/* 控制周期标志（由 SysTick 置位）*/
static volatile uint8_t g_flag_10ms  = 0;
static volatile uint8_t g_flag_50ms  = 0;
static volatile uint8_t g_flag_200ms = 0;

/* 故障状态 */
typedef struct {
    uint8_t over_voltage;
    uint8_t under_voltage;
    uint8_t over_current;
    uint8_t over_temp;
} FaultFlags;
static FaultFlags g_fault = {0};

/* ============================================================
 *  状态切换
 * ============================================================ */
static void enter_state(SystemState s)
{
    g_state = s;
    g_state_enter_ms = g_ms;

    /* 进入新状态时的副作用 */
    switch (s) {
        case ST_IDLE:
        case ST_STOP:
            PWM3Phase_Stop();
            FB_Disable(&g_fb);
            ADC3Phase_Stop();
            VLoop_Reset(&g_vloop);
            RMS_Reset(&g_vline_rms);
            RMS_Reset(&g_iphase_rms);
            LED_On(LED_GREEN);
            LED_Off(LED_YELLOW);
            LED_Off(LED_RED);
            break;

        case ST_PRECHARGE:
            /* 假设硬件上有预充电电阻 + 继电器，软件这里仅等待 Vbus 上升 */
            ADC3Phase_Start();
            LED_On(LED_YELLOW);
            break;

        case ST_RAMPUP:
            /* 启动 PWM、调制比从 MOD_INDEX_MIN 渐升 */
            g_mod_index   = MOD_INDEX_MIN;
            g_ramp_target = MOD_INDEX_INIT;
            VLoop_Init(&g_vloop, VOUT_LINE_REF_V);
            VLoop_Enable(&g_vloop, 0u);
            SVPWM_SetAmplitude(g_mod_index);
            SVPWM_SetFrequency(g_freq_set);
            PWM3Phase_Start();
            break;

        case ST_RUN_INVERTER:
            VLoop_Enable(&g_vloop, 1u);
            FB_Disable(&g_fb);                /* 基本要求：不启用同步整流 */
            LED_On(LED_GREEN);
            LED_Off(LED_YELLOW);
            break;

        case ST_RUN_FEEDBACK:
            VLoop_Enable(&g_vloop, 1u);
            #if USE_SYNC_RECTIFIER
            FB_Enable(&g_fb, 1u);
            #endif
            LED_On(LED_GREEN);
            LED_On(LED_YELLOW);
            break;

        case ST_FAULT:
            PWM3Phase_EmergencyOff();
            FB_Disable(&g_fb);
            LED_On(LED_RED);
            Buzzer_BeepBlocking(800);
            break;

        default: break;
    }
}

/* ============================================================
 *  ADC 回调（弱符号覆盖）
 *  注意：本回调在 ADC DMA 中断中执行（最高优先级），
 *        不要在这里做 OLED / printf / 长循环。
 * ============================================================ */
void ADC3Phase_OnSampleReady(const ADCConverted *s)
{
    if (!s) return;

    g_vbus_meas = s->vbus;

    /* 喂入 RMS（取 Vab 与 Ia 各一相做代表）*/
    RMS_Update(&g_vline_rms, s->v_line[0]);
    RMS_Update(&g_iphase_rms, s->i_phase[0]);

    /* 同步整流决策（仅在 RUN_FEEDBACK 状态下）*/
    if (g_state == ST_RUN_FEEDBACK) {
        FB_Update(&g_fb, s->i_phase[0], s->i_phase[1], s->i_phase[2]);
        /* TODO_HAL: 把 g_fb.state[3] 写到对应的 6 个 SR MOS GPIO 引脚 */
    }

    /* 瞬时过流硬保护（中断中可立即关 PWM）*/
    for (uint8_t p = 0; p < 3u; p++) {
        if (fabsf(s->i_phase[p]) > OC_PEAK_A) {
            g_fault.over_current = 1u;
            PWM3Phase_EmergencyOff();
        }
    }

    /* 母线电压硬保护 */
    if (s->vbus > VDC_MAX_V) {
        g_fault.over_voltage = 1u;
        PWM3Phase_EmergencyOff();
    }
}

/* ============================================================
 *  10 ms 控制：电压外环 + 软启动 ramp
 * ============================================================ */
static void ctrl_10ms(void)
{
    /* 软启动：在 RAMPUP 状态下逐步增加调制比 */
    if (g_state == ST_RAMPUP) {
        g_mod_index += RAMP_STEP;
        if (g_mod_index >= g_ramp_target) {
            g_mod_index = g_ramp_target;
            /* RAMP 完成，根据当前 task 切到 RUN */
            if (g_task == TASK_FEEDBACK) enter_state(ST_RUN_FEEDBACK);
            else                         enter_state(ST_RUN_INVERTER);
        }
        SVPWM_SetAmplitude(g_mod_index);
        return;
    }

    /* RUN 状态下用闭环更新调制比 */
    if ((g_state == ST_RUN_INVERTER) || (g_state == ST_RUN_FEEDBACK)) {
        float vrms = RMS_Get(&g_vline_rms);
        float m = VLoop_Update(&g_vloop, vrms, g_vbus_meas);
        g_mod_index = m;
        SVPWM_SetAmplitude(m);
    }

    /* PRECHARGE 等待母线预充电 */
    if (g_state == ST_PRECHARGE) {
        if (g_vbus_meas > VDC_PRECHARGE_V) {
            enter_state(ST_RAMPUP);
        } else if ((g_ms - g_state_enter_ms) > 2000u) {
            /* 2 秒内还没充上来 → 故障 */
            g_fault.under_voltage = 1u;
            enter_state(ST_FAULT);
        }
    }
}

/* ============================================================
 *  50 ms：故障检测（持续型，不是瞬时型）
 * ============================================================ */
static void ctrl_50ms(void)
{
    if ((g_state == ST_RUN_INVERTER) || (g_state == ST_RUN_FEEDBACK)) {
        /* 母线欠压（持续）*/
        if (g_vbus_meas < VDC_MIN_V) {
            g_fault.under_voltage = 1u;
            enter_state(ST_FAULT);
            return;
        }
        /* 持续过流：RMS 超过额定 1.5 倍 */
        float irms = RMS_Get(&g_iphase_rms);
        if (irms > IOUT_RATED_RMS_A * IRMS_OT_FACTOR) {
            g_fault.over_current = 1u;
            enter_state(ST_FAULT);
            return;
        }
    }
}

/* ============================================================
 *  200 ms：显示 + 按键
 * ============================================================ */
static const char *state_name(SystemState s)
{
    switch (s) {
        case ST_IDLE:           return "IDLE";
        case ST_PRECHARGE:      return "PRECHRG";
        case ST_RAMPUP:         return "RAMPUP";
        case ST_RUN_INVERTER:   return "INV";
        case ST_RUN_FEEDBACK:   return "FB";
        case ST_FAULT:          return "FAULT";
        case ST_STOP:           return "STOP";
        default:                return "?";
    }
}

static void on_key(KeyId k)
{
    switch (k) {
        case KEY_FREQ_UP:
            if (g_freq_set < FREQ_OUT_MAX_HZ) {
                g_freq_set += FREQ_OUT_STEP_HZ;
                SVPWM_SetFrequency(g_freq_set);
            }
            break;
        case KEY_FREQ_DOWN:
            if (g_freq_set > FREQ_OUT_MIN_HZ) {
                g_freq_set -= FREQ_OUT_STEP_HZ;
                SVPWM_SetFrequency(g_freq_set);
            }
            break;
        case KEY_START:
            if ((g_state == ST_IDLE) || (g_state == ST_STOP)) {
                /* 清故障 */
                g_fault = (FaultFlags){0};
                enter_state(ST_PRECHARGE);
            } else if (g_state == ST_FAULT) {
                /* 故障状态下需要先按 STOP 复位才能再 START */
            }
            break;
        case KEY_STOP:
            enter_state(ST_STOP);
            break;
        case KEY_TASK:
            if (g_state == ST_IDLE) {
                g_task = (g_task == TASK_INVERTER) ? TASK_FEEDBACK : TASK_INVERTER;
                Buzzer_BeepBlocking((uint16_t)(80u * (uint16_t)g_task));
            }
            break;
        default: break;
    }
}

static void disp_200ms(void)
{
    OLED_ShowString(0, 0, "2025A 3Ph Inv");
    OLED_ShowString(0, 2, state_name(g_state));
    OLED_ShowFloat (0, 3, "Vab: ",  RMS_Get(&g_vline_rms),  "V");
    OLED_ShowFloat (0, 4, "Ia : ",  RMS_Get(&g_iphase_rms), "A");
    OLED_ShowFloat (0, 5, "Frq: ",  g_freq_set,             "Hz");
    OLED_ShowFloat (0, 6, "Mod: ",  g_mod_index,            "");
    OLED_ShowFloat (0, 7, "Vbus:",  g_vbus_meas,            "V");

    /* 按键扫描 */
    KeyId k = Button_Scan();
    if (k != KEY_NONE) on_key(k);
}

/* ============================================================
 *  TIM1 更新中断（20kHz）
 *  在这里推进 SVPWM 角度并写 PWM 寄存器。
 *  TODO_HAL：在 stm32g4xx_it.c 的 TIM1_UP_TIM16_IRQHandler 中调用本函数。
 * ============================================================ */
void Inverter3Ph_OnTimerUpdate(void)
{
    if ((g_state != ST_RUN_INVERTER) &&
        (g_state != ST_RUN_FEEDBACK) &&
        (g_state != ST_RAMPUP)) {
        return;
    }
    /* 算法层只算占空比，写硬件由 Core 完成（保持算法层不依赖 Drivers）*/
    SVPWM_Out pwm = Inverter3Ph_Update();
    PWM3Phase_SetDuty(pwm.Ta, pwm.Tb, pwm.Tc);
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    /* TODO_HAL: HAL_Init(); SystemClock_Config();   170MHz */
    /* TODO_HAL: 各 MX_*_Init() */

    PWM3Phase_Init();
    ADC3Phase_Init();
    Button_Init();
    LED_Init();
    Buzzer_Init();
    OLED_Init();

    SVPWM_Init();
    SVPWM_SetFrequency(g_freq_set);
    SVPWM_SetAmplitude(MOD_INDEX_INIT);

    VLoop_Init(&g_vloop, VOUT_LINE_REF_V);
    RMS_Init(&g_vline_rms);
    RMS_Init(&g_iphase_rms);
    FB_Init(&g_fb);

    enter_state(ST_IDLE);

    OLED_Clear();
    OLED_ShowString(0, 0, "2025A Ready");
    Buzzer_BeepBlocking(100);

    /* TODO_HAL: SysTick_Config(SystemCoreClock / 1000) → 1ms */

    while (1) {
        if (g_flag_10ms)  { g_flag_10ms = 0;  ctrl_10ms();  }
        if (g_flag_50ms)  { g_flag_50ms = 0;  ctrl_50ms();  }
        if (g_flag_200ms) { g_flag_200ms = 0; disp_200ms(); }
    }
}

/* ============================================================
 *  SysTick (1 ms)
 *  TODO_HAL：在 stm32g4xx_it.c 的 SysTick_Handler 中调用本函数
 * ============================================================ */
void SysTick_Handler(void)
{
    g_ms++;
    if ((g_ms %  10u) == 0u) g_flag_10ms  = 1;
    if ((g_ms %  50u) == 0u) g_flag_50ms  = 1;
    if ((g_ms % 200u) == 0u) g_flag_200ms = 1;
}
