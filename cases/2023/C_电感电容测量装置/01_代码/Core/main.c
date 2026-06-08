/**
 * @file    main.c
 * @brief   2023 C 电感电容测量主程序
 */
#include "../config.h"
#include "../Drivers/dds_ad9833.h"
#include "../Drivers/adc_dual.h"
#include "../Algorithm/impedance.h"

static uint16_t s_buf_ref[IMPEDANCE_FRAME_LEN];
static uint16_t s_buf_dut[IMPEDANCE_FRAME_LEN];
static z_result_t s_z;

int main(void)
{
    dds_init(); dds_set_wave(0); dds_set_freq(DDS_FREQ_HZ);
    adc_dual_init(); adc_dual_start();
    z_init();

    while (1) {
        if (adc_dual_get(s_buf_ref, s_buf_dut, IMPEDANCE_FRAME_LEN)) {
            z_measure(s_buf_ref, s_buf_dut, IMPEDANCE_FRAME_LEN,
                      (float)ADC_FS_HZ, R_REF_OHM, &s_z);
            /* OLED 显示 |Z| / 相位 / L / C */
        }
    }
}
