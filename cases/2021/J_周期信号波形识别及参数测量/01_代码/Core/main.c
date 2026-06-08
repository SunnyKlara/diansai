/**
 * @file    main.c
 * @brief   2021 J 周期信号波形识别 + 参数测量 主程序
 */
#include "../config.h"
#include "../Drivers/adc_capture.h"
#include "../Drivers/range_switch.h"
#include "../Algorithm/waveform_id.h"
#include "../Algorithm/param_meas.h"

static uint16_t s_frame[FFT_SIZE];
static wf_result_t s_wf;
static meas_result_t s_meas;

int main(void)
{
    adc_init();
    rng_init();
    wf_id_init();

    adc_set_fs(ADC_FS_MID_HZ);
    adc_start();

    while (1) {
        if (adc_get_frame(s_frame, FFT_SIZE)) {
            /* 1. 找最大最小用于自动量程 */
            uint16_t vmin = 0xFFFF, vmax = 0;
            for (uint32_t i = 0; i < FFT_SIZE; i++) {
                if (s_frame[i] < vmin) vmin = s_frame[i];
                if (s_frame[i] > vmax) vmax = s_frame[i];
            }
            range_t target = rng_auto(vmax, vmin);
            if (target != rng_get()) {
                rng_set(target);
                continue;       /* 切换后重新采样 */
            }

            /* 2. 波形识别 */
            float gain = rng_gain(rng_get());
            wf_id_analyze(s_frame, FFT_SIZE, V_PER_LSB / gain, ADC_OFFSET_LSB, &s_wf);

            /* 3. 参数测量 */
            meas_run(s_frame, FFT_SIZE,
                     (float)ADC_FS_MID_HZ, V_PER_LSB / gain,
                     ADC_OFFSET_LSB, &s_meas);

            /* 4. OLED 刷新（占位） */
        }
    }
}
