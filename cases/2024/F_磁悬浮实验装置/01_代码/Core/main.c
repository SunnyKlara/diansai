/**
 * @file    main.c
 * @brief   2024 F 磁悬浮 主程序：5 电磁铁阵列 + PD 微分先行
 */

#include "../config.h"
#include "../Drivers/hall_sensor.h"
#include "../Drivers/pwm_coil.h"
#include "../Algorithm/maglev_pd.h"

static volatile uint32_t s_tick_ms = 0;
static volatile float s_h_ref = H_REF_DEFAULT_MM;
static volatile float s_h_ref_filt = H_REF_DEFAULT_MM;
static maglev_pd_t s_pd_center;
static maglev_pd_t s_pd_side[4];

void main_systick_isr(void){ s_tick_ms++; }

void main_ctrl_isr(void)
{
    /* 1. 高度 ramp 平滑 */
    if (s_h_ref_filt < s_h_ref - H_RAMP_STEP_MM) s_h_ref_filt += H_RAMP_STEP_MM;
    else if (s_h_ref_filt > s_h_ref + H_RAMP_STEP_MM) s_h_ref_filt -= H_RAMP_STEP_MM;
    else s_h_ref_filt = s_h_ref;

    /* 2. 中央电磁铁 PD */
    float h_center = hall_get_mm(0);
    float duty_c = maglev_pd_update(&s_pd_center, s_h_ref_filt, h_center, CTRL_PERIOD_S);
    pwm_coil_set(0, duty_c);

    /* 3. 四周电磁铁 PD（保持水平：目标 = 中央距离） */
    for (uint8_t i = 0; i < 4; i++) {
        float h = hall_get_mm(i + 1);
        float duty = maglev_pd_update(&s_pd_side[i], s_h_ref_filt, h, CTRL_PERIOD_S);
        pwm_coil_set(i + 1, duty);
    }
}

int main(void)
{
    hall_init();
    pwm_coil_init();
    maglev_pd_init(&s_pd_center, MAGLEV_KP_CENTER, MAGLEV_KD_CENTER, MAGLEV_KI_CENTER);
    for (uint8_t i = 0; i < 4; i++)
        maglev_pd_init(&s_pd_side[i], MAGLEV_KP_SIDE, MAGLEV_KD_SIDE, MAGLEV_KI_SIDE);

    pwm_coil_start();

    while (1) {
        /* 按键调高度 / OLED 显示 */
    }
}
