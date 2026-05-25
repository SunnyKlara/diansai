/**
 * @file    main.c
 * @brief   2024 B 单相功率分析仪 主程序（MSP430FR5994）
 *
 *  状态机（围绕 1Hz 测量循环 + 低功耗设计）：
 *      ST_BOOT       上电初始化（一次性）
 *      ST_SAMPLING   双 ADC 同步采样（CPU 在 LPM0，ADC + DMA 跑）
 *      ST_CALC       时域 + 频域计算（CPU active）
 *      ST_DISPLAY    HT1621 写显存
 *      ST_LPM        进入 LPM3，等下一秒被 Timer_B 唤醒
 *
 *  时序（占空比预算）：
 *      0   ~ 100 ms：ADC 采集 1000 点（CPU LPM0，~ 0.3 mA）
 *      100 ~ 102 ms：CPU 计算（active，2.5 mA）
 *      102 ~ 105 ms：LCD 写入（active）
 *      105 ms ~ 1 s：CPU LPM3（0.5 μA）
 *      → 平均电流 < 0.5 mA → 1.65 mW（CPU 部分）
 *
 *  显示策略：
 *      6 页轮显（Vrms+Irms / P+PF / THD+H1+H2 / H3+H4+H5 / H6+H7+H8 / H9+H10）
 *      每 5 秒切换一页，按 KEY_PAGE 手动切换。
 */

#include <stdint.h>

#include "../config.h"

#include "../Drivers/adc_dual.h"
#include "../Drivers/lcd_ht1621.h"
#include "../Drivers/button.h"
#include "../Drivers/led.h"

#include "../Algorithm/power_calc.h"
#include "../Algorithm/dft_harmonic.h"

/* TODO_TI: #include <ti/devices/msp430fr5xx_6xx/driverlib/MSP430FR5xx_6xx/driverlib.h> */

/* ============================================================
 *  全局
 * ============================================================ */
typedef enum {
    ST_BOOT = 0,
    ST_SAMPLING,
    ST_CALC,
    ST_DISPLAY,
    ST_LPM,
} SystemState;

static volatile SystemState g_state = ST_BOOT;
static volatile uint32_t    g_sec   = 0;          /* 秒计数 */

/* 算法对象 */
static ADCConvertedFrame    g_frame;
static TimeDomainResult     g_td;
static HarmonicResult       g_harm;
static DispPage             g_page = DISP_PAGE_VRMS_IRMS;
static uint32_t             g_page_change_sec = 0;

/* ============================================================
 *  显示更新
 * ============================================================ */
static void update_display(void)
{
    LCD_Clear();

    switch (g_page) {
        case DISP_PAGE_VRMS_IRMS:
            LCD_ShowFloat(0, g_td.v_rms, "V");
            LCD_ShowFloat(1, g_td.i_rms, "A");
            break;
        case DISP_PAGE_P_PF:
            LCD_ShowFloat(0, g_td.p_active, "W");
            LCD_ShowFloat(1, g_td.pf,       "PF");
            break;
        case DISP_PAGE_THD_H1_H2:
            LCD_ShowFloat(0, g_harm.thd_percent, "%");
            LCD_ShowFloat(1, g_harm.h_norm[2] * 100.0f, "H2");
            break;
        case DISP_PAGE_H3_H4_H5:
            LCD_ShowFloat(0, g_harm.h_norm[3] * 100.0f, "H3");
            LCD_ShowFloat(1, g_harm.h_norm[4] * 100.0f, "H4");
            break;
        case DISP_PAGE_H6_H7_H8:
            LCD_ShowFloat(0, g_harm.h_norm[6] * 100.0f, "H6");
            LCD_ShowFloat(1, g_harm.h_norm[7] * 100.0f, "H7");
            break;
        case DISP_PAGE_H9_H10_TOTAL:
            LCD_ShowFloat(0, g_harm.h_norm[9]  * 100.0f, "H9");
            LCD_ShowFloat(1, g_harm.h_norm[10] * 100.0f, "10");
            break;
        default: break;
    }
}

/* ============================================================
 *  按键事件
 * ============================================================ */
static void on_key(KeyId k)
{
    switch (k) {
        case KEY_PAGE:
            g_page = (DispPage)(((int)g_page + 1) % (int)DISP_PAGE_COUNT);
            g_page_change_sec = g_sec;
            update_display();
            break;
        case KEY_CALIB:
            /* TODO: 进入校准模式：UART 接收"标准 V/I"值 → 计算 gain/offset → 存 FRAM */
            break;
        default: break;
    }
}

/* ============================================================
 *  完整测量周期
 * ============================================================ */
static void run_measure_cycle(void)
{
    /* ---- 1. 启动 ADC 采集 ---- */
    g_state = ST_SAMPLING;
    LED_On(LED_RUN);
    ADCDual_StartOnce();

    /* ADC 期间 CPU 进 LPM0（CPU 关，外设跑）*/
    /* TODO_TI: __bis_SR_register(LPM0_bits | GIE); */
    while (!ADCDual_IsDone()) {
        /* 在真机上：不会执行此循环（LPM0 中），DMA 完成中断会唤醒 */
    }

    /* ---- 2. 转物理量 ---- */
    ADCDual_GetFrame(&g_frame);

    /* ---- 3. 时域计算 ---- */
    g_state = ST_CALC;
    PowerCalc_TimeDomain(g_frame.v_samples, g_frame.i_samples, DFT_N, &g_td);

    /* ---- 4. 频域：谐波 + THD（仅对电流做 DFT，电压通常很纯）---- */
    DFTHarm_Compute(g_frame.i_samples, DFT_N, &g_harm);

    /* ---- 5. 显示 ---- */
    g_state = ST_DISPLAY;

    /* 每 5 秒自动翻页 */
    if ((g_sec - g_page_change_sec) >= (uint32_t)DISP_ROTATE_PERIOD_S) {
        g_page = (DispPage)(((int)g_page + 1) % (int)DISP_PAGE_COUNT);
        g_page_change_sec = g_sec;
    }
    update_display();

    LED_Off(LED_RUN);
}

/* ============================================================
 *  Timer_B 1Hz 中断：唤醒主循环做下一次测量
 *  TODO_TI：在 stm430 it.c 中挂接到对应的 IRQ
 * ============================================================ */
void OnTimerB_OneSecond(void)
{
    g_sec++;
    /* TODO_TI: __bic_SR_register_on_exit(LPM3_bits); */
    /* 唤醒主循环 */
}

/* ============================================================
 *  按键中断（也是 LPM 唤醒源之一）
 * ============================================================ */
void OnButton_IRQ(void)
{
    KeyId k = Button_Scan();
    if (k != KEY_NONE) on_key(k);
    /* TODO_TI: __bic_SR_register_on_exit(LPM3_bits); */
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    /* TODO_TI: WDT_A_hold; SysClock 配 16MHz; */
    /* TODO_TI: __bis_SR_register(GIE); */

    g_state = ST_BOOT;

    LED_Init();
    Button_Init();
    LCD_Init();
    ADCDual_Init();
    DFTHarm_Init();

    LCD_Clear();
    LED_On(LED_RUN);

    /* TODO_TI: 启动 Timer_B 周期 1s 中断 */

    while (1) {
        /* 完整测量周期 */
        run_measure_cycle();

        /* 进入 LPM3 等下一秒 */
        g_state = ST_LPM;
        /* TODO_TI: __bis_SR_register(LPM3_bits | GIE); */
        /* 唤醒后从这里继续 → 进入下一轮 run_measure_cycle */
    }
}
